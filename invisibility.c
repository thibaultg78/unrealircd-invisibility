/*
 * unrealircd-invisibility
 * File: invisibility.c
 * Version: 1.0.0
 * Date: 2026-10-07
 *
 * Third-party module for UnrealIRCd 6 that recreates the historical IRC
 * operator channel-invisibility behavior as user mode +X.
 *
 * An IRC operator using +X remains a normal channel member internally, but
 * local non-IRC operators receive a filtered view of that membership:
 * JOIN/PART/QUIT and membership-related events are hidden, NAMES and WHO/WHOX
 * are filtered, and channel membership is hidden from WHOIS. IRC operators
 * continue to see the real channel state. Channel messages remain visible;
 * speaking does not permanently reveal the hidden membership.
 *
 * Tested with: UnrealIRCd 6.2.7 on *NIX
 * Project: unrealircd-invisibility
 * Author: Thibault
 * GitHub: https://github.com/akril/unrealircd-invisibility
 * Website: https://akril.net
 * 
 *
 * This is an independent third-party module and is not an official UnrealIRCd
 * module. Use at your own risk.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026 Thibault
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "unrealircd.h"

static long UMODE_INVISIBILITY = 0L;
static int bypass_filter = 0;

#define IsInvisibility(x) ((x) && IsUser(x) && ((x)->umodes & UMODE_INVISIBILITY))

/* ==================== Forward declarations ==================== */

static int invisibility_umode_change(Client *client, long oldflags, long newflags);
static int invisibility_see_channel_in_whois(Client *client, Client *target, Channel *channel);
static int invisibility_packet(Client *from, Client *to, Client *intended_to, char **msg, int *length);

static void send_fake_part(Client *client, Channel *channel);
static void send_fake_join(Client *client, Channel *channel);
static void send_membership_modes(Client *client, Channel *channel, Client *viewer);

static const char *skip_tags(const char *p);
static const char *skip_prefix(const char *p);
static Client *source_client(const char *line, const char **command);
static int command_is(const char *command, const char *name);
static int hidden_client_for_viewer(Client *viewer, Client *target);
static int users_share_channel(Client *a, Client *b);
static int hidden_source_event_relevant(Client *viewer, Client *source, const char *command);

static void filter_names(char **msg, int *length);
static int who352_hidden(const char *line);
static int whox354_hidden(const char *line);
static int rewrite_channel_mode_for_viewer(char **msg, int *length);
static int kick_targets_hidden_user(const char *line);

/* ========================================================================
 * Module registration and lifecycle
 * Registers +X and the hooks used to present filtered membership state.
 * ======================================================================== */

ModuleHeader MOD_HEADER = {
    "third/invisibility",
    "1.0.0",
    "IRCOp channel invisibility user mode (+X)",
    "Thibault",
    "unrealircd-6",
};

MOD_INIT()
{
    ModuleSetOptions(modinfo->handle, MOD_OPT_PERM_RELOADABLE, 1);

    /*
     * +X is used because +I is already assigned by modern UnrealIRCd.
     * The mode is global, IRCOp-only, and automatically removed on de-oper.
     */
    if (!UmodeAdd(modinfo->handle,
                  'X',
                  UMODE_GLOBAL,
                  1,                  /* automatically unset on de-oper */
                  umode_allow_opers,
                  &UMODE_INVISIBILITY))
    {
        config_error("invisibility: could not register usermode +X: %s",
                     ModuleGetErrorStr(modinfo->handle));
        return MOD_FAILED;
    }

    HookAdd(modinfo->handle, HOOKTYPE_UMODE_CHANGE, 0,
            invisibility_umode_change);

    HookAdd(modinfo->handle, HOOKTYPE_SEE_CHANNEL_IN_WHOIS, 0,
            invisibility_see_channel_in_whois);

    HookAdd(modinfo->handle, HOOKTYPE_PACKET, 0,
            invisibility_packet);

    return MOD_SUCCESS;
}

MOD_LOAD()
{
    return MOD_SUCCESS;
}

MOD_UNLOAD()
{
    return MOD_SUCCESS;
}

/* ========================================================================
 * Visibility policy
 * Defines when a +X IRC operator is hidden and which viewers bypass hiding.
 * ======================================================================== */

static int hidden_client_for_viewer(Client *viewer, Client *target)
{
    if (!viewer || !target)
        return 0;

    if (!IsInvisibility(target))
        return 0;

    /* Self and every IRCOp see the real state. */
    if (viewer == target || IsOper(viewer))
        return 0;

    return 1;
}

/*
 * Used for NICK/AWAY/CHGHOST/SETNAME/ACCOUNT/QUIT. These events are not
 * channel-specific on the wire. Hide them only if the non-oper viewer shares
 * at least one channel with the invisible user; otherwise Unreal would not
 * normally have sent a common-channel notification anyway.
 */
static int users_share_channel(Client *a, Client *b)
{
    Membership *m;

    if (!a || !b || !a->user || !b->user)
        return 0;

    for (m = a->user->channel; m; m = m->next)
        if (find_membership_link(b->user->channel, m->channel))
            return 1;

    return 0;
}

/* ========================================================================
 * +X / -X visibility transitions
 * +X sends a synthetic PART; -X sends a synthetic JOIN to local non-IRCops.
 * The real server-side channel membership never changes.
 * ======================================================================== */

static int invisibility_umode_change(Client *client, long oldflags, long newflags)
{
    Membership *m;
    int old_i = (oldflags & UMODE_INVISIBILITY) != 0;
    int new_i = (newflags & UMODE_INVISIBILITY) != 0;

    if (!IsUser(client) || !client->user || old_i == new_i)
        return 0;

    /*
     * IMPORTANT: no set_user_invisible(), no MEMB_FLAG_INVISIBLE.
     * The core always retains the truthful membership.
     */
    for (m = client->user->channel; m; m = m->next)
    {
        if (new_i)
            send_fake_part(client, m->channel);
        else
            send_fake_join(client, m->channel);
    }

    return 0;
}

/* ========================================================================
 * WHOIS filtering
 * Hides channel memberships of +X IRC operators from non-IRC operators.
 * ======================================================================== */

static int invisibility_see_channel_in_whois(Client *viewer,
                                              Client *target,
                                              Channel *channel)
{
    if (!hidden_client_for_viewer(viewer, target))
        return EX_ALLOW;

    return EX_ALWAYS_DENY;
}

/* ========================================================================
 * Synthetic membership transitions
 * Emits ordinary-looking PART/JOIN events without changing real membership.
 * ======================================================================== */

static void send_fake_part(Client *client, Channel *channel)
{
    Member *m;
    MessageTag *mtags = NULL;

    new_message_special(client, NULL, &mtags,
                        ":%s PART %s", client->name, channel->name);

    /*
     * The normal packet firewall hides PART from +I users. This PART is the
     * deliberate state transition produced when +I is enabled.
     */
    bypass_filter++;

    for (m = channel->members; m; m = m->next)
    {
        Client *viewer = m->client;

        if (!MyConnect(viewer) || viewer == client || IsOper(viewer))
            continue;

        sendto_one(viewer, mtags,
                   ":%s!%s@%s PART %s",
                   client->name,
                   client->user->username,
                   GetHost(client),
                   channel->name);
    }

    bypass_filter--;
    free_message_tags(mtags);
}

static void send_fake_join(Client *client, Channel *channel)
{
    Member *m;
    MessageTag *mtags = NULL;
    long cap_extended_join = ClientCapabilityBit("extended-join");

    new_message_special(client, NULL, &mtags,
                        ":%s JOIN %s", client->name, channel->name);

    /*
     * At this point -X has already been applied, so the normal filter no
     * longer hides this user. We still use bypass_filter so this transition
     * remains deterministic if hook ordering differs on a specific 6.x build.
     */
    bypass_filter++;

    for (m = channel->members; m; m = m->next)
    {
        Client *viewer = m->client;

        if (!MyConnect(viewer) || viewer == client || IsOper(viewer))
            continue;

        if (HasCapabilityFast(viewer, cap_extended_join))
        {
            sendto_one(viewer, mtags,
                       ":%s!%s@%s JOIN %s %s :%s",
                       client->name,
                       client->user->username,
                       GetHost(client),
                       channel->name,
                       IsLoggedIn(client) ? client->user->account : "*",
                       client->info);
        }
        else
        {
            sendto_one(viewer, mtags,
                       ":%s!%s@%s JOIN :%s",
                       client->name,
                       client->user->username,
                       GetHost(client),
                       channel->name);
        }

        /*
         * Re-advertise membership prefixes (+v/+h/+o/+a/+q) after the fake
         * JOIN so the receiving client's local channel state is coherent.
         */
        send_membership_modes(client, channel, viewer);
    }

    bypass_filter--;
    free_message_tags(mtags);
}

static void send_membership_modes(Client *client, Channel *channel, Client *viewer)
{
    Member *member;
    char modes[16];
    char params[256];
    size_t mi = 0;

    member = find_member_link(channel->members, client);
    if (!member)
        return;

    modes[0] = '\0';
    params[0] = '\0';

    /*
     * check_channel_access_member() is intentionally used only to reconstruct
     * the real membership modes after -I. It grants NO visibility privilege.
     */
    if (check_channel_access_member(member, "q"))
    {
        modes[mi++] = 'q';
        strlcat(params, client->name, sizeof(params));
        strlcat(params, " ", sizeof(params));
    }
    if (check_channel_access_member(member, "a"))
    {
        modes[mi++] = 'a';
        strlcat(params, client->name, sizeof(params));
        strlcat(params, " ", sizeof(params));
    }
    if (check_channel_access_member(member, "o"))
    {
        modes[mi++] = 'o';
        strlcat(params, client->name, sizeof(params));
        strlcat(params, " ", sizeof(params));
    }
    if (check_channel_access_member(member, "h"))
    {
        modes[mi++] = 'h';
        strlcat(params, client->name, sizeof(params));
        strlcat(params, " ", sizeof(params));
    }
    if (check_channel_access_member(member, "v"))
    {
        modes[mi++] = 'v';
        strlcat(params, client->name, sizeof(params));
        strlcat(params, " ", sizeof(params));
    }

    modes[mi] = '\0';

    if (!mi)
        return;

    if (*params && params[strlen(params)-1] == ' ')
        params[strlen(params)-1] = '\0';

    sendto_one(viewer, NULL,
               ":%s MODE %s +%s %s",
               me.name, channel->name, modes, params);
}

/* ========================================================================
 * IRC protocol parsing helpers
 * Handles tags, prefixes, commands and source resolution for packet filtering.
 * ======================================================================== */

static const char *skip_tags(const char *p)
{
    if (!p)
        return NULL;

    if (*p == '@')
    {
        p = strchr(p, ' ');
        if (!p)
            return NULL;
        while (*p == ' ')
            p++;
    }

    return p;
}

static const char *skip_prefix(const char *p)
{
    p = skip_tags(p);
    if (!p)
        return NULL;

    if (*p == ':')
    {
        p = strchr(p, ' ');
        if (!p)
            return NULL;
        while (*p == ' ')
            p++;
    }

    return p;
}

static Client *source_client(const char *line, const char **command)
{
    const char *p, *start, *end, *bang;
    char nick[NICKLEN + 1];
    size_t n;

    if (command)
        *command = NULL;

    p = skip_tags(line);
    if (!p || *p != ':')
        return NULL;

    start = ++p;
    end = strchr(start, ' ');
    if (!end)
        return NULL;

    bang = memchr(start, '!', (size_t)(end - start));
    if (bang)
        end = bang;

    n = (size_t)(end - start);
    if (!n || n > NICKLEN)
        return NULL;

    memcpy(nick, start, n);
    nick[n] = '\0';

    p = strchr(start, ' ');
    if (!p)
        return NULL;
    while (*p == ' ')
        p++;

    if (command)
        *command = p;

    return find_client(nick, NULL);
}

static int command_is(const char *command, const char *name)
{
    size_t n;

    if (!command || !name)
        return 0;

    n = strlen(name);

    return !strncasecmp(command, name, n) &&
           (command[n] == '\0' || command[n] == ' ' ||
            command[n] == '\r' || command[n] == '\n');
}

/* ========================================================================
 * NAMES filtering
 * Rewrites RPL_NAMREPLY (353) to omit +X IRC operators for non-IRCops.
 * ======================================================================== */

/*
 * Return the nick part of a NAMES token:
 *
 *   @Nick
 *   @Nick!ident@host
 *   +Nick
 *
 * Multi-prefix is handled by stripping all leading prefix characters.
 */
static Client *names_token_client(char *token)
{
    char *p = token;
    char *bang;

    while (*p == '~' || *p == '&' || *p == '@' ||
           *p == '%' || *p == '+')
        p++;

    bang = strchr(p, '!');
    if (bang)
        *bang = '\0';

    return find_client(p, NULL);
}

static void filter_names(char **msg, int *length)
{
    static char out[MAXLINELENGTH];
    char work[MAXLINELENGTH];
    char original[MAXLINELENGTH];
    char *p, *trail, *names, *save = NULL, *tok;
    size_t prefix_len;
    int first = 1;

    strlcpy(original, *msg, sizeof(original));
    strlcpy(work, *msg, sizeof(work));

    /*
     * Parse from the command, not by searching for the first " :".
     * This avoids confusing "@batch=x :server ..." with the trailing
     * parameter of numeric 353.
     */
    p = (char *)skip_prefix(work);
    if (!p || strncmp(p, "353 ", 4))
        return;

    /*
     * RPL_NAMREPLY has the trailing names list after " :".
     * Search only inside the already-located numeric payload.
     */
    trail = strstr(p, " :");
    if (!trail)
        return;

    names = trail + 2;

    /* Same byte offset in the untouched original string. */
    prefix_len = (size_t)(names - work);
    if (prefix_len >= sizeof(out))
        return;

    memcpy(out, original, prefix_len);
    out[prefix_len] = '\0';

    for (tok = strtok_r(names, " \r\n", &save);
         tok;
         tok = strtok_r(NULL, " \r\n", &save))
    {
        char tmp[HOSTLEN + USERLEN + NICKLEN + 16];
        Client *candidate;

        strlcpy(tmp, tok, sizeof(tmp));
        candidate = names_token_client(tmp);

        if (candidate && IsInvisibility(candidate))
            continue;

        if (!first)
            strlcat(out, " ", sizeof(out));
        strlcat(out, tok, sizeof(out));
        first = 0;
    }

    /*
     * HOOKTYPE_PACKET receives the complete line buffer on current Unreal 6.
     * Preserve the original CRLF convention instead of blindly appending one.
     */
    if (strstr(original, "\r\n"))
        strlcat(out, "\r\n", sizeof(out));
    else if (strchr(original, '\n'))
        strlcat(out, "\n", sizeof(out));

    *msg = out;
    *length = (int)strlen(out);
}

/* ========================================================================
 * WHO / WHOX filtering
 * Suppresses WHO/WHOX rows that would expose a hidden IRC operator.
 * ======================================================================== */

static int who352_hidden(const char *line)
{
    char copy[MAXLINELENGTH];
    char *p, *save = NULL, *tok;
    int field = 0;

    strlcpy(copy, line, sizeof(copy));
    p = (char *)skip_prefix(copy);
    if (!p || strncmp(p, "352 ", 4))
        return 0;

    /*
     * RFC 2812 RPL_WHOREPLY:
     * 352 <me> <channel> <user> <host> <server> <nick> <flags> :...
     *
     * Token #7 after 352 is the nick (counting 352 as field 0 below).
     */
    for (tok = strtok_r(p, " ", &save);
         tok;
         tok = strtok_r(NULL, " ", &save))
    {
        Client *candidate;

        if (field == 6)
        {
            candidate = find_client(tok, NULL);
            return candidate && IsInvisibility(candidate);
        }

        field++;
    }

    return 0;
}

static int whox354_hidden(const char *line)
{
    char copy[MAXLINELENGTH];
    char *p, *save = NULL, *tok;

    strlcpy(copy, line, sizeof(copy));
    p = (char *)skip_prefix(copy);
    if (!p || strncmp(p, "354 ", 4))
        return 0;

    /*
     * WHOX fields are caller-selected, so there is no universal nick offset.
     * V3 avoids the V1 false-positive bug by accepting ONLY tokens that resolve
     * exactly to a currently connected +I nickname. This can still miss a WHOX
     * format that omits the nick entirely; in that case the channel-membership
     * leak must be addressed by a dedicated WHO hook if the target 6.x build
     * exposes one.
     *
     * Unlike V1, ':' realname text is never scanned: stop at the trailing
     * parameter.
     */
    for (tok = strtok_r(p, " ", &save);
         tok;
         tok = strtok_r(NULL, " ", &save))
    {
        Client *candidate;

        if (*tok == ':')
            break;

        candidate = find_client(tok, NULL);
        if (candidate && IsInvisibility(candidate))
            return 1;
    }

    return 0;
}

/* ========================================================================
 * KICK and MODE filtering
 * Prevents membership-related events from exposing hidden IRC operators.
 * Mixed MODE lines preserve unrelated mode changes.
 * ======================================================================== */

static int kick_targets_hidden_user(const char *line)
{
    char copy[MAXLINELENGTH];
    char *p, *save = NULL;
    char *cmd, *chan, *nick;
    Client *target;

    strlcpy(copy, line, sizeof(copy));
    p = (char *)skip_prefix(copy);
    if (!p)
        return 0;

    cmd = strtok_r(p, " ", &save);
    chan = strtok_r(NULL, " ", &save);
    nick = strtok_r(NULL, " ", &save);

    if (!cmd || strcasecmp(cmd, "KICK") || !chan || !nick)
        return 0;

    target = find_client(nick, NULL);
    return target && IsInvisibility(target);
}

static int mode_takes_parameter(char mode, int adding)
{
    /*
     * Membership/list modes always take a parameter.
     *
     * Common UnrealIRCd 6 modes below take a parameter when adding.
     * This list is intentionally aligned with the stock modes needed by this
     * lab; third-party channel modes may extend CHANMODES and are outside the
     * scope of this experimental module.
     */
    if (strchr("qaohvbeI", mode))
        return 1;

    if (mode == 'k')
        return 1;

    if (strchr("lLfFHj", mode))
        return adding;

    return 0;
}

/*
 * Rewrite MODE rather than dropping a mixed MODE line wholesale.
 * Only q/a/o/h/v operations targeting an invisible nick are removed.
 * Other modes and their parameters are preserved.
 */
static int rewrite_channel_mode_for_viewer(char **msg, int *length)
{
    static char out[MAXLINELENGTH];
    char copy[MAXLINELENGTH], original[MAXLINELENGTH];
    char *p, *save = NULL, *cmd, *target, *modes;
    char *params[64];
    int nparams = 0, pi = 0, adding = 1, changed = 0;
    char kept_modes[128], kept_params[MAXLINELENGTH];
    size_t km = 0;
    size_t command_offset;
    char *tok;
    int i;

    strlcpy(original, *msg, sizeof(original));
    strlcpy(copy, *msg, sizeof(copy));

    p = (char *)skip_prefix(copy);
    if (!p)
        return 0;

    /*
     * p points to the actual IRC command in copy. copy and original have the
     * same layout, so this offset is safe and avoids strstr("MODE "), which
     * could accidentally match text inside a prefix/vhost.
     */
    command_offset = (size_t)(p - copy);

    cmd = strtok_r(p, " ", &save);
    target = strtok_r(NULL, " ", &save);
    modes = strtok_r(NULL, " ", &save);

    if (!cmd || strcasecmp(cmd, "MODE") || !target || *target != '#' || !modes)
        return 0;

    while ((tok = strtok_r(NULL, " \r\n", &save)) != NULL && nparams < 64)
        params[nparams++] = tok;

    kept_modes[0] = '\0';
    kept_params[0] = '\0';

    for (i = 0; modes[i]; i++)
    {
        char c = modes[i];
        int takes, hide_this = 0;
        char *param = NULL;

        if (c == '+' || c == '-')
        {
            adding = (c == '+');

            /*
             * Do not accumulate adjacent/orphaned signs. If the last emitted
             * character is already a sign, replace it with the new one.
             */
            if (km && (kept_modes[km - 1] == '+' || kept_modes[km - 1] == '-'))
            {
                kept_modes[km - 1] = c;
            }
            else if (km < sizeof(kept_modes) - 1)
            {
                kept_modes[km++] = c;
            }
            continue;
        }

        takes = mode_takes_parameter(c, adding);
        if (takes && pi < nparams)
            param = params[pi++];

        if (strchr("qaohv", c) && param)
        {
            Client *candidate = find_client(param, NULL);
            if (candidate && IsInvisibility(candidate))
                hide_this = 1;
        }

        if (hide_this)
        {
            changed = 1;
            continue;
        }

        if (km < sizeof(kept_modes) - 1)
            kept_modes[km++] = c;

        if (param)
        {
            if (*kept_params)
                strlcat(kept_params, " ", sizeof(kept_params));
            strlcat(kept_params, param, sizeof(kept_params));
        }
    }

    kept_modes[km] = '\0';

    /*
     * A hidden operation may leave a sign immediately before another sign,
     * eg. "+o-v Inv Other" -> "+-v Other". Compact all such sequences and
     * remove a trailing sign.
     */
    {
        char compact[sizeof(kept_modes)];
        size_t r, w = 0;

        for (r = 0; kept_modes[r] && w < sizeof(compact) - 1; r++)
        {
            char c = kept_modes[r];

            if ((c == '+' || c == '-') &&
                w && (compact[w - 1] == '+' || compact[w - 1] == '-'))
            {
                compact[w - 1] = c;
                continue;
            }

            compact[w++] = c;
        }

        while (w && (compact[w - 1] == '+' || compact[w - 1] == '-'))
            w--;

        compact[w] = '\0';
        strlcpy(kept_modes, compact, sizeof(kept_modes));
        km = strlen(kept_modes);
    }

    if (!changed)
        return 0;

    /*
     * If every meaningful mode operation disappeared, suppress the line.
     * A string containing only +/- is already reduced to empty above.
     */
    if (!km)
    {
        *msg = NULL;
        *length = 0;
        return 1;
    }

    if (command_offset >= sizeof(out))
        return 0;

    memcpy(out, original, command_offset);
    out[command_offset] = '\0';

    strlcat(out, "MODE ", sizeof(out));
    strlcat(out, target, sizeof(out));
    strlcat(out, " ", sizeof(out));
    strlcat(out, kept_modes, sizeof(out));

    if (*kept_params)
    {
        strlcat(out, " ", sizeof(out));
        strlcat(out, kept_params, sizeof(out));
    }

    if (strstr(original, "\r\n"))
        strlcat(out, "\r\n", sizeof(out));
    else if (strchr(original, '\n'))
        strlcat(out, "\n", sizeof(out));

    *msg = out;
    *length = (int)strlen(out);
    return 1;
}

/* ========================================================================
 * Source-event filtering
 * Suppresses hidden membership events. PRIVMSG/NOTICE remain visible by
 * design and do not permanently reveal channel membership.
 * ======================================================================== */

static int hidden_source_event_relevant(Client *viewer,
                                        Client *source,
                                        const char *command)
{
    if (!hidden_client_for_viewer(viewer, source) || !command)
        return 0;

    /*
     * JOIN/PART are channel-membership events.
     * QUIT/NICK/AWAY/CHGHOST/SETNAME/ACCOUNT are common-channel visibility
     * events and should not reveal a membership the viewer cannot see.
     *
     * PRIVMSG and NOTICE are intentionally absent: speaking is visible.
     */
    if (command_is(command, "JOIN") ||
        command_is(command, "PART"))
        return 1;

    if (command_is(command, "QUIT") ||
        command_is(command, "NICK") ||
        command_is(command, "AWAY") ||
        command_is(command, "CHGHOST") ||
        command_is(command, "SETNAME") ||
        command_is(command, "ACCOUNT"))
        return users_share_channel(viewer, source);

    return 0;
}

/*
 * Known limitation:
 * INVITE/KICK command-side probing cannot be made semantically perfect from an
 * outgoing packet hook alone. We intentionally do NOT invent undocumented
 * pre-command hook signatures here. KICK output revealing the hidden target is
 * filtered below, but a chanop can still infer success from server-side effects.
 * This is a known lab limitation unless the exact target UnrealIRCd tree offers
 * a suitable command-override/pre-command API.
 */

/* ========================================================================
 * Final outgoing visibility filter
 * Keeps UnrealIRCd's internal membership truthful while filtering the view
 * delivered to local non-IRC operators.
 * ======================================================================== */

static int invisibility_packet(Client *from,
                               Client *to,
                               Client *intended_to,
                               char **msg,
                               int *length)
{
    const char *command = NULL;
    const char *p;
    Client *source;

    if (bypass_filter)
        return 0;

    /*
     * Never touch S2S traffic. Only alter the representation delivered to
     * local non-IRCOp users.
     */
    if (from != &me || !to || !IsUser(to) || IsOper(to) || !msg || !*msg)
        return 0;

    source = source_client(*msg, &command);

    if (source && hidden_source_event_relevant(to, source, command))
    {
        *msg = NULL;
        *length = 0;
        return 0;
    }

    p = skip_prefix(*msg);
    if (!p)
        return 0;

    /* RPL_NAMREPLY */
    if (!strncmp(p, "353 ", 4))
    {
        filter_names(msg, length);
        return 0;
    }

    /* Standard WHO: channel membership rows for +I users are suppressed. */
    if (!strncmp(p, "352 ", 4))
    {
        if (who352_hidden(*msg))
        {
            *msg = NULL;
            *length = 0;
        }
        return 0;
    }

    /* WHOX */
    if (!strncmp(p, "354 ", 4))
    {
        if (whox354_hidden(*msg))
        {
            *msg = NULL;
            *length = 0;
        }
        return 0;
    }

    /* Someone kicking the hidden member would otherwise disclose membership. */
    if (kick_targets_hidden_user(*msg))
    {
        *msg = NULL;
        *length = 0;
        return 0;
    }

    /*
     * Membership MODE changes (+o/+v/etc.) for the hidden user are hidden
     * from non-opers. On -I we reconstruct the actual prefixes explicitly.
     */
    if (rewrite_channel_mode_for_viewer(msg, length))
        return 0;

    return 0;
}

/*** <<<MODULE MANAGER START>>>
module
{
    documentation "https://www.unrealircd.org/docs/Dev:Module";
    troubleshooting "Experimental lab module. Compile and test against the exact UnrealIRCd 6.x source tree.";
    min-unrealircd-version "6.*";
    max-unrealircd-version "6.*";
    post-install-text {
        "Add: loadmodule \"third/invisibility\";";
        "Then /REHASH and test with a normal user, a channel op, and an IRCOp.";
    }
}
*** <<<MODULE MANAGER END>>>
*/
