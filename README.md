# unrealircd-invisibility

Bringing back the historical IRCOp channel invisibility behavior for UnrealIRCd 6 — built with Codex.

`unrealircd-invisibility` is an independent third-party module for UnrealIRCd 6 that adds an IRC operator-only user mode, `+X`.

The goal is simple: reproduce the old behavior many long-time UnrealIRCd users remember, where an IRC operator could remain in a channel while being completely absent from the channel membership view of normal users.

The module is intentionally implemented without changing UnrealIRCd's real channel membership state. Instead, it keeps the server state truthful and filters what local non-IRC operators are allowed to see.

> **Status:** experimental, but functional. Initially developed and tested against UnrealIRCd **6.2.7** on *NIX.

The mode was originally assigned the letter `+I`; this version uses uppercase `+X` instead.

## What `+X` does

When an IRC operator enables:

```text
/MODE YourNick +X
```

normal users should no longer see that IRC operator as a channel member.

IRC operators continue to see the real state.

The intended behavior is:

| Event / command | Normal user | IRC operator |
| --- | --- | --- |
| IRCOp joins while `+X` | JOIN hidden | JOIN visible |
| IRCOp parts while `+X` | PART hidden | PART visible |
| IRCOp quits while `+X` | QUIT hidden | QUIT visible |
| `/NAMES` | IRCOp hidden | IRCOp visible |
| `/WHO` / WHOX | IRCOp hidden | IRCOp visible |
| WHOIS channel list | Hidden | Visible |
| IRCOp sends `PRIVMSG` / `NOTICE` to channel | Message visible | Message visible |
| `/LIST` user count | Real count | Real count |

Speaking in a channel does **not** reveal the hidden membership afterwards. A normal user may therefore see a message from the IRC operator without seeing that operator in the channel membership list.

`+X` is global, restricted to IRC operators, and configured to be automatically removed when the user loses IRC operator status.

If a regular user tries to enable `+X`, the request is silently ignored because the mode is reserved for IRC operators.

## Why `+X`?

Modern UnrealIRCd already uses user mode `+I`, so this module uses `+X` instead.

The letter itself is not the important part. The purpose of the module is to restore the historical IRCOp channel-invisibility behavior without conflicting with current UnrealIRCd user modes.

## Installation

Copy the module into the UnrealIRCd third-party module directory:

```bash
cp invisibility.c /path/to/unrealircd-6.x/src/modules/third/invisibility.c
```

From the UnrealIRCd source directory:

```bash
make
make install
```

UnrealIRCd automatically detects `.c` files placed in `src/modules/third/` and builds them as third-party modules.

Then add the module to your UnrealIRCd configuration:

```text
loadmodule "third/invisibility";
```

Check the configuration and start or rehash UnrealIRCd as usual.

The module follows the standard UnrealIRCd 6 third-party module layout and API.

## Usage

The mode can only be enabled by an IRC operator:

```text
/MODE YourNick +X
```

Disable it with:

```text
/MODE YourNick -X
```

Enabling or disabling the mode does **not** make the IRC operator actually leave or rejoin channels.

Instead:

- enabling `+X` sends an ordinary-looking synthetic `PART` to affected local non-IRC operators;
- disabling `+X` sends a synthetic `JOIN`;
- the real membership inside UnrealIRCd remains unchanged.

This allows existing IRC clients to keep a coherent local channel state without requiring client-side support for the feature.

---

# How it works

## Design principle: keep the real membership untouched

The most important design decision is what the module **does not** do.

It does not remove the user from the channel internally and it does not rely on UnrealIRCd's delayed-join membership state.

A `+X` IRC operator remains a completely normal member of the channel from the server's point of view.

Conceptually:

```text
                    UnrealIRCd internal state
                             |
                             |
                    IRCOp is in #channel
                             |
               +-------------+-------------+
               |                           |
               v                           v
        IRC operator viewer         Normal user viewer
               |                           |
        real membership              filtered view
               |                           |
        IRCOp is visible             IRCOp is hidden
```

This matters because channel privileges, server logic, synchronization, and normal membership handling continue to operate on the real state.

The invisibility exists primarily at the **presentation layer**.

## User mode registration

The module registers `+X` through UnrealIRCd's User Mode API:

```c
UmodeAdd(
    modinfo->handle,
    'X',
    UMODE_GLOBAL,
    1,
    umode_allow_opers,
    &UMODE_INVISIBILITY
);
```

The important properties are:

- `UMODE_GLOBAL` — the mode is a global user mode;
- `umode_allow_opers` — only IRC operators may enable it;
- `unset_on_deoper = 1` — UnrealIRCd removes it when the user loses IRC operator status.

The resulting mode bit is stored in `UMODE_INVISIBILITY` and checked throughout the module.

## Visibility policy

The central rule is intentionally simple:

```text
target has +X
AND viewer is not the target
AND viewer is not an IRC operator
        =
target is hidden from that viewer
```

IRC operators therefore always retain an unfiltered view of channel membership.

This is different from using channel privilege as the visibility boundary. A normal user with `+v`, `+h`, `+o`, `+a`, or `+q` is still a non-IRC operator for the purpose of this module.

## Synthetic PART when `+X` is enabled

Consider an IRC operator who is already present in several channels and then enables `+X`.

Simply filtering future JOIN events would not be enough: normal users already have that IRC operator in their local nick lists.

The module therefore walks the IRC operator's existing memberships and sends affected local users a synthetic, ordinary-looking PART:

```text
:OperNick!user@host PART #channel
```

There is deliberately no special PART reason.

From the normal IRC client's point of view, the IRC operator simply left the channel.

No real PART occurs inside UnrealIRCd.

IRC operators do not receive this synthetic event.

## Synthetic JOIN when `+X` is disabled

The reverse operation happens when `+X` is removed.

Normal users previously believed the IRC operator had left, so the module sends them a synthetic JOIN.

Where appropriate, the module also restores the membership modes associated with that user so the receiving client's local state can once again match the visible state.

Again, the IRC operator never actually left or rejoined the channel on the server.

## The outgoing packet filter

The core of the implementation is an UnrealIRCd `HOOKTYPE_PACKET` hook.

Rather than attempting to modify every internal subsystem that may expose channel membership, the module treats outgoing IRC protocol traffic as the final visibility boundary for local non-IRC operators.

The packet hook can:

1. inspect the outgoing IRC line;
2. identify its source and command;
3. determine whether a `+X` user would be exposed;
4. suppress the line entirely, or rewrite it when only part of the response must be hidden.

This creates two views of the same truthful server state:

```text
                         Real channel state
                                |
                         UnrealIRCd core
                                |
                         outgoing message
                                |
                    invisibility packet hook
                         /              \
                        /                \
                       v                  v
                 IRC operator       normal user
                 unchanged           filtered
```

A small internal bypass is used for synthetic transitions generated by the module itself so they are not accidentally consumed by its own filtering logic.

## NAMES filtering

`/NAMES` is not handled by pretending the user is absent from UnrealIRCd.

Instead, the module rewrites the outgoing `RPL_NAMREPLY` (`353`) response for local non-IRC operators.

The parser accounts for IRC protocol details such as:

- IRCv3 message tags;
- nick prefixes;
- channel membership prefixes;
- multi-prefix;
- userhost-in-names style entries.

Entries corresponding to `+X` IRC operators are removed while the rest of the reply is preserved.

IRC operators receive the original response.

## WHO and WHOX filtering

Standard WHO replies (`352`) and WHOX replies (`354`) are inspected before being delivered to local non-IRC operators.

If a reply would expose a hidden `+X` IRC operator, that row is suppressed.

WHOX is less rigid than traditional WHO because clients can request different field layouts. The implementation therefore has to be more defensive when identifying the nick represented by a WHOX response.

## WHOIS channel visibility

Channel membership can also leak through WHOIS.

The module uses UnrealIRCd's channel-visibility hook for WHOIS so a non-IRC operator cannot simply bypass the NAMES/WHO filtering by requesting the hidden operator's WHOIS information.

IRC operators continue to receive the real channel list.

## JOIN, PART and QUIT

Normal membership events originating from a `+X` IRC operator are suppressed for local non-IRC operators.

This produces the expected illusion:

```text
Oper enables +X
        |
normal user receives synthetic PART
        |
Oper may really PART / JOIN channels
        |
those membership events remain hidden
        |
Oper disables +X
        |
normal user receives synthetic JOIN
```

The normal user therefore does not receive contradictory membership events while the operator is supposed to be invisible.

## PRIVMSG and NOTICE are intentionally visible

Channel messages are **not** hidden.

A `+X` IRC operator may speak normally:

```text
<OperNick> hello
```

A normal user can see that message even though `OperNick` is not shown as a channel member.

Crucially, speaking does not change the real membership state and does not permanently reveal the operator through NAMES or WHO.

This behavior is intentional and is one of the reasons the module does not use a delayed-join-style membership flag as its primary mechanism.

## MODE filtering

Channel MODE messages need special handling because a mode change can mention a hidden nick.

Dropping an entire MODE line would be incorrect when the same line contains unrelated mode changes.

The module therefore parses mixed MODE operations and removes only membership-mode operations (`q/a/o/h/v`) that target hidden `+X` users, while preserving unrelated operations and their parameters.

For example, the goal is to avoid turning one hidden membership change into lost state for everyone else in the same MODE command.

The parser knows about the parameter behavior of common UnrealIRCd 6 channel modes. Third-party channel modes may introduce additional parameter semantics that this module does not know about.

## KICK and other membership-related events

Outgoing KICK and other source events are inspected so they do not trivially expose a hidden IRC operator through normal channel traffic.

The current implementation focuses primarily on **what the server sends to the viewer**.

It does not attempt to redefine every command-side semantic in UnrealIRCd.

---

# Known limitations and design choices

## `/LIST` keeps the real user count

The module currently does not falsify channel population numbers returned by `/LIST`.

A channel may therefore report two users while a normal user can only identify one visible member.

This is intentional for now.

The module's objective is to hide the IRC operator's **identity and membership**, not to construct a mathematically isolated alternate universe for every viewer.

## Command-side probing

Because the primary visibility firewall is on outgoing protocol traffic, commands such as `INVITE` or `KICK` may still allow a sufficiently determined channel operator to infer that an unseen member exists from command behavior or numeric replies.

UnrealIRCd provides command override APIs that could be used to address such probes more aggressively.

That is currently outside the main scope of this implementation.

## Third-party channel modes

The MODE rewriter understands the parameter semantics needed for standard/common UnrealIRCd 6 channel modes.

A third-party module that adds unusual parameterized channel modes may require the MODE parser to be extended.

## Multi-server networks

`+X` is registered as a global user mode.

However, this project has initially been developed and tested on a small UnrealIRCd 6.2.7 test environment. Complex multi-server scenarios, netbursts, services interactions, and every possible S2S transition have **not** yet been exhaustively validated.

For a real network, test the module on every server role and deploy consistently.

## IRC clients

The module works at IRC protocol level, but IRC clients differ in how they display numerics and maintain their graphical nick lists.

Testing should therefore distinguish between actual server protocol behavior and client-specific presentation quirks.

---

# Current testing

The initial development environment uses UnrealIRCd **6.2.7**.

The following behavior has been manually exercised during development:

- the module compiles as an UnrealIRCd third-party module;
- the module loads successfully;
- `+X` can be enabled by an IRC operator;
- enabling `+X` produces the expected disappearance for a normal user;
- disabling `+X` produces a synthetic JOIN and reappearance;
- NAMES filtering hides the `+X` operator;
- WHO filtering hides the `+X` operator;
- the hidden IRC operator can still speak in the channel;
- speaking does not reveal persistent membership;
- `/LIST` retains the real channel user count by design.

This is still experimental software. More edge cases should be tested before treating it as production-ready.

---

# Compatibility

Currently targeted at:

- UnrealIRCd 6
- manually tested on UnrealIRCd 6.2.7
- *NIX build environment

Other UnrealIRCd 6.x releases may work, but API and protocol behavior should be tested rather than assumed.

---

# License

`unrealircd-invisibility` is released under the **GNU General Public License v2.0 or later** (`GPL-2.0-or-later`).

UnrealIRCd itself is also distributed under GPLv2 or later.

This project is an independent third-party module and is **not** an official UnrealIRCd module.

---

# Development note

This module was built as a personal revival project with help from **Codex**.

The original motivation was less about adding a modern IRC feature and more about bringing back a piece of classic UnrealIRCd behavior that some long-time IRC users may remember — while seeing how far an AI-assisted implementation could go against the modern UnrealIRCd 6 module API.

Issues, testing feedback, and improvements are welcome.
