# unrealircd-invisibility

Rétablir le comportement historique d'invisibilité des IRCop dans les salons pour UnrealIRCd 6, avec l'aide de Codex.

`unrealircd-invisibility` est un module tiers indépendant pour UnrealIRCd 6 qui ajoute un mode utilisateur réservé aux opérateurs IRC : `+X`.

L'objectif est simple : reproduire l'ancien comportement dont se souviennent de nombreux utilisateurs de longue date d'UnrealIRCd, lorsqu'un opérateur IRC pouvait rester dans un salon tout en étant totalement absent de la liste des membres visible par les utilisateurs ordinaires.

Le module est volontairement conçu sans modifier l'état réel des appartenances aux salons dans UnrealIRCd. Le serveur conserve ainsi un état fidèle, tandis que le module filtre ce que les utilisateurs locaux qui ne sont pas opérateurs IRC peuvent voir.

> **État :** expérimental, mais fonctionnel. Initialement développé et testé avec UnrealIRCd **6.2.7** sous *NIX.

À l'origine, le mode était associé à la lettre `+I` ; cette version utilise plutôt la majuscule `+X`.

## Fonctionnement de `+X`

Lorsqu'un opérateur IRC active le mode :

```text
/MODE YourNick +X
```

les utilisateurs ordinaires ne voient plus cet opérateur IRC dans la liste des membres du salon.

Les opérateurs IRC continuent, eux, de voir l'état réel.

Le comportement attendu est le suivant :

| Événement / commande | Utilisateur ordinaire | Opérateur IRC |
| --- | --- | --- |
| L'IRCOp rejoint le salon avec `+X` | JOIN masqué | JOIN visible |
| L'IRCOp quitte le salon avec `+X` | PART masqué | PART visible |
| L'IRCOp se déconnecte avec `+X` | QUIT masqué | QUIT visible |
| `/NAMES` | IRCOp masqué | IRCOp visible |
| `/WHO` / WHOX | IRCOp masqué | IRCOp visible |
| Liste des salons dans WHOIS | Masquée | Visible |
| L'IRCOp envoie un `PRIVMSG` / `NOTICE` au salon | Message visible | Message visible |
| Nombre d'utilisateurs dans `/LIST` | Nombre réel | Nombre réel |

Prendre la parole dans un salon ne révèle **pas** ensuite l'appartenance masquée. Un utilisateur ordinaire peut donc voir un message de l'opérateur IRC sans le voir dans la liste des membres du salon.

`+X` est un mode global, réservé aux opérateurs IRC et automatiquement retiré lorsque l'utilisateur perd son statut d'opérateur IRC.

Si un utilisateur ordinaire tente d'activer `+X`, sa demande est ignorée sans notification, car ce mode est réservé aux opérateurs IRC.

## Pourquoi `+X` ?

UnrealIRCd moderne utilise déjà le mode utilisateur `+I` ; ce module utilise donc `+X` à la place.

La lettre en elle-même n'est pas importante. Le but du module est de rétablir le comportement historique d'invisibilité des IRCOp dans les salons sans entrer en conflit avec les modes utilisateur actuels d'UnrealIRCd.

## Installation

Copiez le module dans le répertoire des modules tiers d'UnrealIRCd :

```bash
cp invisibility.c /path/to/unrealircd-6.x/src/modules/third/invisibility.c
```

Depuis le répertoire des sources d'UnrealIRCd :

```bash
make
make install
```

UnrealIRCd détecte automatiquement les fichiers `.c` placés dans `src/modules/third/` et les compile comme modules tiers.

Ajoutez ensuite le module à la configuration d'UnrealIRCd :

```text
loadmodule "third/invisibility";
```

Vérifiez la configuration, puis démarrez ou rechargez UnrealIRCd comme d'habitude.

Le module respecte la structure et l'API standard des modules tiers d'UnrealIRCd 6.

## Utilisation

Seul un opérateur IRC peut activer ce mode :

```text
/MODE YourNick +X
```

Pour le désactiver :

```text
/MODE YourNick -X
```

L'activation ou la désactivation du mode ne fait **pas** réellement quitter ou rejoindre les salons à l'opérateur IRC.

À la place :

- l'activation de `+X` envoie aux utilisateurs locaux concernés qui ne sont pas opérateurs IRC un `PART` synthétique d'apparence ordinaire ;
- la désactivation de `+X` envoie un `JOIN` synthétique ;
- l'appartenance réelle au salon dans UnrealIRCd reste inchangée.

Les clients IRC existants conservent ainsi un état local cohérent du salon, sans nécessiter de prise en charge de cette fonctionnalité côté client.

---

# Fonctionnement interne

## Principe de conception : ne pas modifier les appartenances réelles

La décision de conception la plus importante concerne ce que le module **ne** fait **pas**.

Il ne retire pas l'utilisateur du salon dans l'état interne et ne s'appuie pas sur l'état d'appartenance différée d'UnrealIRCd.

Du point de vue du serveur, un IRCOp avec `+X` reste un membre tout à fait normal du salon.

Schématiquement :

```text
                    État interne d'UnrealIRCd
                             |
                             |
                    IRCOp présent dans #channel
                             |
               +-------------+-------------+
               |                           |
               v                           v
      Vue d'un opérateur IRC       Vue d'un utilisateur ordinaire
               |                           |
        appartenance réelle          vue filtrée
               |                           |
        IRCOp visible                IRCOp masqué
```

C'est important, car les privilèges dans les salons, la logique du serveur, la synchronisation et le traitement normal des appartenances continuent de s'appuyer sur l'état réel.

L'invisibilité intervient principalement au **niveau de la présentation**.

## Enregistrement du mode utilisateur

Le module enregistre `+X` au moyen de l'API des modes utilisateur d'UnrealIRCd :

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

Les propriétés importantes sont :

- `UMODE_GLOBAL` — le mode est un mode utilisateur global ;
- `umode_allow_opers` — seuls les opérateurs IRC peuvent l'activer ;
- `unset_on_deoper = 1` — UnrealIRCd le retire lorsque l'utilisateur perd son statut d'opérateur IRC.

Le bit de mode correspondant est stocké dans `UMODE_INVISIBILITY` et vérifié dans tout le module.

## Règle de visibilité

La règle centrale est volontairement simple :

```text
la cible a le mode +X
ET le destinataire n'est pas la cible
ET le destinataire n'est pas un opérateur IRC
        =
la cible est masquée pour ce destinataire
```

Les opérateurs IRC conservent donc toujours une vue non filtrée des membres du salon.

La visibilité ne dépend pas des privilèges dans le salon. Pour ce module, un utilisateur ordinaire ayant `+v`, `+h`, `+o`, `+a` ou `+q` reste un utilisateur qui n'est pas opérateur IRC.

## `PART` synthétique à l'activation de `+X`

Imaginons qu'un opérateur IRC soit déjà présent dans plusieurs salons et active ensuite `+X`.

Il ne suffirait pas de filtrer les futurs événements JOIN : les utilisateurs ordinaires ont déjà cet opérateur IRC dans leur liste locale de pseudos.

Le module parcourt donc les salons auxquels l'opérateur IRC appartient déjà et envoie aux utilisateurs locaux concernés un `PART` synthétique d'apparence ordinaire :

```text
:OperNick!user@host PART #channel
```

Aucune raison particulière n'est volontairement ajoutée au PART.

Du point de vue du client IRC ordinaire, l'opérateur IRC a simplement quitté le salon.

Aucun véritable PART n'a lieu dans UnrealIRCd.

Les opérateurs IRC ne reçoivent pas cet événement synthétique.

## `JOIN` synthétique à la désactivation de `+X`

L'opération inverse a lieu lorsque `+X` est retiré.

Les utilisateurs ordinaires pensaient jusque-là que l'opérateur IRC avait quitté le salon ; le module leur envoie donc un JOIN synthétique.

Le cas échéant, le module rétablit aussi les modes d'appartenance associés à cet utilisateur, afin que l'état local du client destinataire corresponde de nouveau à l'état visible.

Là encore, l'opérateur IRC n'a jamais réellement quitté le salon ni ne l'a rejoint à nouveau sur le serveur.

## Filtrage des paquets sortants

Le cœur de l'implémentation est un hook `HOOKTYPE_PACKET` d'UnrealIRCd.

Au lieu d'essayer de modifier tous les sous-systèmes internes susceptibles d'exposer les appartenances aux salons, le module considère le trafic IRC sortant comme la dernière frontière de visibilité pour les utilisateurs locaux qui ne sont pas opérateurs IRC.

Le hook de paquet peut :

1. examiner la ligne IRC sortante ;
2. identifier sa source et sa commande ;
3. déterminer si un utilisateur avec `+X` serait exposé ;
4. supprimer entièrement la ligne ou la réécrire si seule une partie de la réponse doit être masquée.

On obtient ainsi deux vues du même état serveur fidèle :

```text
                         État réel du salon
                                |
                         Cœur d'UnrealIRCd
                                |
                         Message sortant
                                |
                    Hook de filtrage du module
                         /              \
                        /                \
                       v                  v
                opérateur IRC       utilisateur ordinaire
                 inchangé                 filtré
```

Un petit mécanisme de contournement interne est utilisé pour les transitions synthétiques produites par le module lui-même, afin qu'elles ne soient pas interceptées par son propre filtrage.

## Filtrage de NAMES

`/NAMES` n'est pas traité en faisant comme si l'utilisateur était absent d'UnrealIRCd.

À la place, le module réécrit la réponse sortante `RPL_NAMREPLY` (`353`) pour les utilisateurs locaux qui ne sont pas opérateurs IRC.

Le parseur tient compte de détails du protocole IRC tels que :

- les balises de message IRCv3 ;
- les préfixes de pseudos ;
- les préfixes de statut des membres du salon ;
- le mode multi-prefix ;
- les entrées au format userhost-in-names.

Les entrées correspondant aux IRCOp avec `+X` sont supprimées, tandis que le reste de la réponse est conservé.

Les opérateurs IRC reçoivent la réponse d'origine.

## Filtrage de WHO et WHOX

Les réponses WHO standard (`352`) et WHOX (`354`) sont examinées avant d'être envoyées aux utilisateurs locaux qui ne sont pas opérateurs IRC.

Si une réponse révèle un IRCOp avec `+X`, la ligne correspondante est supprimée.

WHOX est moins strict que WHO traditionnel, car les clients peuvent demander différentes dispositions de champs. L'implémentation doit donc identifier le pseudo représenté dans une réponse WHOX avec davantage de précautions.

## Visibilité des salons dans WHOIS

Les appartenances aux salons peuvent aussi être révélées par WHOIS.

Le module utilise le hook de visibilité des salons d'UnrealIRCd pour WHOIS afin qu'un utilisateur qui n'est pas opérateur IRC ne puisse pas contourner le filtrage de NAMES et WHO en demandant les informations WHOIS de l'opérateur masqué.

Les opérateurs IRC continuent de recevoir la véritable liste des salons.

## JOIN, PART et QUIT

Les événements d'appartenance ordinaires provenant d'un IRCOp avec `+X` sont masqués aux utilisateurs locaux qui ne sont pas opérateurs IRC.

L'illusion obtenue est celle attendue :

```text
L'IRCOp active +X
        |
l'utilisateur ordinaire reçoit un PART synthétique
        |
l'IRCOp peut réellement quitter ou rejoindre des salons
        |
ces événements d'appartenance restent masqués
        |
l'IRCOp désactive +X
        |
l'utilisateur ordinaire reçoit un JOIN synthétique
```

L'utilisateur ordinaire ne reçoit donc pas d'événements d'appartenance contradictoires pendant que l'opérateur est censé être invisible.

## Les `PRIVMSG` et `NOTICE` restent volontairement visibles

Les messages envoyés dans le salon ne sont **pas** masqués.

Un IRCOp avec `+X` peut parler normalement :

```text
<OperNick> hello
```

Un utilisateur ordinaire peut voir ce message même si `OperNick` n'apparaît pas dans la liste des membres du salon.

Point essentiel : prendre la parole ne modifie pas l'appartenance réelle et ne révèle pas durablement l'opérateur par NAMES ou WHO.

Ce comportement est intentionnel. C'est l'une des raisons pour lesquelles le module n'utilise pas un indicateur d'appartenance de type delayed join comme mécanisme principal.

## Filtrage de MODE

Les messages MODE du salon demandent un traitement particulier, car un changement de mode peut mentionner un pseudo masqué.

Supprimer toute une ligne MODE serait incorrect si cette même ligne contient d'autres changements de mode sans rapport.

Le module analyse donc les opérations MODE combinées et ne retire que les opérations portant sur les modes d'appartenance (`q/a/o/h/v`) qui ciblent des utilisateurs masqués avec `+X`. Les autres opérations et leurs paramètres sont conservés.

L'objectif est notamment d'éviter qu'un changement d'appartenance masqué fasse perdre aux autres utilisateurs l'état correspondant dans la même commande MODE.

Le parseur connaît le comportement des paramètres des modes de salon courants d'UnrealIRCd 6. Des modes de salon tiers peuvent introduire des règles de paramètres supplémentaires que ce module ne connaît pas.

## KICK et autres événements liés aux appartenances

Les KICK sortants et les autres événements émis par une source sont examinés afin qu'ils ne révèlent pas trop facilement un IRCOp masqué dans le trafic normal du salon.

L'implémentation actuelle se concentre principalement sur **ce que le serveur envoie au destinataire**.

Elle ne cherche pas à redéfinir la sémantique de toutes les commandes d'UnrealIRCd.

---

# Limites connues et choix de conception

## `/LIST` conserve le nombre réel d'utilisateurs

Pour le moment, le module ne falsifie pas les nombres d'utilisateurs des salons renvoyés par `/LIST`.

Un salon peut donc indiquer deux utilisateurs alors qu'un utilisateur ordinaire ne peut identifier qu'un seul membre visible.

C'est intentionnel pour l'instant.

L'objectif du module est de masquer **l'identité et l'appartenance** de l'opérateur IRC, et non de construire pour chaque destinataire un univers alternatif isolé jusque dans ses décomptes.

## Exploration par les commandes

Comme le principal mécanisme de protection de la visibilité filtre le trafic du protocole sortant, des commandes comme `INVITE` ou `KICK` peuvent encore permettre à un opérateur de salon suffisamment déterminé de déduire la présence d'un membre invisible à partir du comportement des commandes ou des réponses numériques.

UnrealIRCd fournit des API de substitution de commandes qui pourraient servir à contrer ces tentatives de manière plus poussée.

Ce point ne fait actuellement pas partie du périmètre principal de l'implémentation.

## Modes de salon tiers

Le réécriveur MODE comprend les règles de paramètres nécessaires aux modes de salon standard et courants d'UnrealIRCd 6.

Un module tiers ajoutant des modes de salon paramétrés inhabituels peut nécessiter une extension du parseur MODE.

## Réseaux multi-serveurs

`+X` est enregistré comme mode utilisateur global.

Cependant, ce projet a d'abord été développé et testé dans un petit environnement de test UnrealIRCd 6.2.7. Les scénarios complexes à plusieurs serveurs, les netbursts, les interactions avec les services et toutes les transitions S2S possibles n'ont **pas** encore été validés de manière exhaustive.

Pour un réseau réel, testez le module sur chaque rôle de serveur et déployez-le de manière cohérente.

## Clients IRC

Le module fonctionne au niveau du protocole IRC, mais les clients IRC diffèrent dans leur affichage des réponses numériques et dans la gestion de leurs listes graphiques de pseudos.

Les tests doivent donc distinguer le comportement réel du protocole côté serveur des particularités d'affichage propres aux clients.

---

# Tests actuels

L'environnement de développement initial utilise UnrealIRCd **6.2.7**.

Les comportements suivants ont été testés manuellement pendant le développement :

- le module se compile comme module tiers UnrealIRCd ;
- le module se charge correctement ;
- un opérateur IRC peut activer `+X` ;
- l'activation de `+X` produit la disparition attendue pour un utilisateur ordinaire ;
- la désactivation de `+X` produit un JOIN synthétique et fait réapparaître l'opérateur ;
- le filtrage de NAMES masque l'IRCOp avec `+X` ;
- le filtrage de WHO masque l'IRCOp avec `+X` ;
- l'IRCOp masqué peut toujours parler dans le salon ;
- prendre la parole ne révèle pas durablement son appartenance ;
- `/LIST` conserve volontairement le nombre réel d'utilisateurs du salon.

Ce logiciel reste expérimental. D'autres cas limites doivent être testés avant de le considérer comme prêt pour la production.

---

# Compatibilité

Cibles actuelles :

- UnrealIRCd 6 ;
- testé manuellement avec UnrealIRCd 6.2.7 ;
- environnement de compilation *NIX.

D'autres versions 6.x d'UnrealIRCd peuvent fonctionner, mais il vaut mieux tester leur API et leur comportement protocolaire plutôt que de le supposer.

---

# Licence

`unrealircd-invisibility` est publié sous la **GNU General Public License version 2.0 ou ultérieure** (`GPL-2.0-or-later`).

UnrealIRCd est également distribué sous GPLv2 ou ultérieure.

Ce projet est un module tiers indépendant et **n'est pas** un module officiel d'UnrealIRCd.

---

# Note de développement

Ce module a été créé comme projet personnel de remise au goût du jour avec l'aide de **Codex**.

L'idée de départ était moins d'ajouter une fonctionnalité IRC moderne que de faire revivre un comportement classique d'UnrealIRCd dont certains utilisateurs de longue date se souviennent peut-être, tout en explorant jusqu'où une implémentation assistée par IA pouvait aller avec l'API moderne des modules UnrealIRCd 6.

Les signalements de problèmes, retours de test et améliorations sont les bienvenus.