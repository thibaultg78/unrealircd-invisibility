# unrealircd-invisibility

Das historische Verhalten der IRCOp-Kanalsichtbarkeit für UnrealIRCd 6 zurückbringen, mit Unterstützung von Codex.

`unrealircd-invisibility` ist ein unabhängiges Drittanbietermodul für UnrealIRCd 6, das einen IRC-Operatoren vorbehaltenen Benutzermodus `+X` hinzufügt.

Das Ziel ist einfach: das frühere Verhalten wiederherzustellen, an das sich viele langjährige UnrealIRCd-Benutzer erinnern. Damals konnte ein IRC-Operator in einem Kanal bleiben und zugleich in der Mitgliederliste für normale Benutzer vollständig unsichtbar sein.

Das Modul ist bewusst so implementiert, dass es den tatsächlichen Mitgliedschaftsstatus in UnrealIRCd nicht verändert. Der Serverzustand bleibt dadurch wahrheitsgemäß; stattdessen filtert das Modul, was lokale Benutzer ohne IRC-Operatorstatus sehen dürfen.

> **Status:** experimentell, aber funktionsfähig. Zunächst mit UnrealIRCd **6.2.7** unter *NIX entwickelt und getestet.

Ursprünglich war dem Modus der Buchstabe `+I` zugewiesen. Diese Version verwendet stattdessen das große `+X`.

## Was `+X` bewirkt

Wenn ein IRC-Operator Folgendes aktiviert:

```text
/MODE YourNick +X
```

sollten normale Benutzer diesen IRC-Operator nicht mehr als Kanalmitglied sehen.

IRC-Operatoren sehen weiterhin den tatsächlichen Zustand.

Das gewünschte Verhalten sieht so aus:

| Ereignis / Befehl | Normaler Benutzer | IRC-Operator |
| --- | --- | --- |
| IRCOp tritt mit `+X` dem Kanal bei | JOIN verborgen | JOIN sichtbar |
| IRCOp verlässt mit `+X` den Kanal | PART verborgen | PART sichtbar |
| IRCOp trennt mit `+X` die Verbindung | QUIT verborgen | QUIT sichtbar |
| `/NAMES` | IRCOp verborgen | IRCOp sichtbar |
| `/WHO` / WHOX | IRCOp verborgen | IRCOp sichtbar |
| Kanalliste in WHOIS | Verborgen | Sichtbar |
| IRCOp sendet `PRIVMSG` / `NOTICE` an den Kanal | Nachricht sichtbar | Nachricht sichtbar |
| Benutzerzahl bei `/LIST` | Tatsächliche Anzahl | Tatsächliche Anzahl |

Nachrichten im Kanal **machen** die verborgene Mitgliedschaft **nicht** nachträglich sichtbar. Ein normaler Benutzer kann daher eine Nachricht des IRC-Operators sehen, ohne diesen in der Mitgliederliste des Kanals zu sehen.

`+X` ist global, IRC-Operatoren vorbehalten und wird automatisch entfernt, wenn der Benutzer seinen IRC-Operatorstatus verliert.

Versucht ein normaler Benutzer, `+X` zu aktivieren, wird die Anfrage stillschweigend ignoriert, da der Modus IRC-Operatoren vorbehalten ist.

## Warum `+X`?

Modernes UnrealIRCd verwendet den Benutzermodus `+I` bereits, deshalb nutzt dieses Modul stattdessen `+X`.

Der Buchstabe selbst ist nicht entscheidend. Das Modul soll das historische Verhalten der IRCOp-Kanalunsichtbarkeit wiederherstellen, ohne mit den aktuellen UnrealIRCd-Benutzermodi in Konflikt zu geraten.

## Installation

Kopiere das Modul in das Drittanbietermodul-Verzeichnis von UnrealIRCd:

```bash
cp invisibility.c /path/to/unrealircd-6.x/src/modules/third/invisibility.c
```

Führe im UnrealIRCd-Quellverzeichnis Folgendes aus:

```bash
make
make install
```

UnrealIRCd erkennt `.c`-Dateien in `src/modules/third/` automatisch und baut sie als Drittanbietermodule.

Füge das Modul anschließend zur UnrealIRCd-Konfiguration hinzu:

```text
loadmodule "third/invisibility";
```

Prüfe die Konfiguration und starte UnrealIRCd wie gewohnt oder lade die Konfiguration neu.

Das Modul folgt dem standardmäßigen Layout und der API für Drittanbietermodule von UnrealIRCd 6.

## Verwendung

Nur ein IRC-Operator kann diesen Modus aktivieren:

```text
/MODE YourNick +X
```

Deaktivieren kannst du ihn mit:

```text
/MODE YourNick -X
```

Das Aktivieren oder Deaktivieren des Modus bewirkt **nicht**, dass der IRC-Operator die Kanäle tatsächlich verlässt oder ihnen erneut beitritt.

Stattdessen:

- sendet die Aktivierung von `+X` an betroffene lokale Benutzer ohne IRC-Operatorstatus ein gewöhnlich wirkendes synthetisches `PART`;
- sendet die Deaktivierung von `+X` ein synthetisches `JOIN`;
- bleibt die tatsächliche Mitgliedschaft in UnrealIRCd unverändert.

So behalten vorhandene IRC-Clients einen konsistenten lokalen Kanalzustand, ohne dass der Client diese Funktion speziell unterstützen muss.

---

# Funktionsweise

## Konstruktionsprinzip: die tatsächliche Mitgliedschaft nicht verändern

Die wichtigste Designentscheidung betrifft das, was das Modul **nicht** tut.

Es entfernt den Benutzer nicht intern aus dem Kanal und stützt sich nicht auf UnrealIRCd-Mitgliedschaften mit verzögertem Beitritt.

Aus Sicht des Servers bleibt ein IRC-Operator mit `+X` ein ganz normales Mitglied des Kanals.

Konzeptionell:

```text
                    Interner Zustand von UnrealIRCd
                             |
                             |
                    IRCOp ist in #channel
                             |
               +-------------+-------------+
               |                           |
               v                           v
       Ansicht des IRC-Operators   Ansicht normaler Benutzer
               |                           |
       tatsächliche Mitgliedschaft    gefilterte Ansicht
               |                           |
        IRCOp sichtbar                IRCOp verborgen
```

Das ist wichtig, weil Kanalprivilegien, Serverlogik, Synchronisierung und die normale Behandlung von Mitgliedschaften weiterhin mit dem tatsächlichen Zustand arbeiten.

Die Unsichtbarkeit besteht hauptsächlich auf der **Darstellungsebene**.

## Registrierung des Benutzermodus

Das Modul registriert `+X` über die UnrealIRCd-API für Benutzermodi:

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

Wichtige Eigenschaften:

- `UMODE_GLOBAL` — der Modus ist ein globaler Benutzermodus;
- `umode_allow_opers` — nur IRC-Operatoren dürfen ihn aktivieren;
- `unset_on_deoper = 1` — UnrealIRCd entfernt ihn, wenn der Benutzer seinen IRC-Operatorstatus verliert.

Das resultierende Modusbit wird in `UMODE_INVISIBILITY` gespeichert und im gesamten Modul geprüft.

## Sichtbarkeitsregel

Die zentrale Regel ist bewusst einfach:

```text
Ziel hat +X
UND Betrachter ist nicht das Ziel
UND Betrachter ist kein IRC-Operator
        =
Ziel ist für diesen Betrachter verborgen
```

IRC-Operatoren behalten somit immer eine ungefilterte Ansicht der Kanalmitglieder.

Anders als bei einer Sichtbarkeitsgrenze anhand der Kanalprivilegien gilt ein normaler Benutzer mit `+v`, `+h`, `+o`, `+a` oder `+q` für dieses Modul weiterhin als Nicht-IRC-Operator.

## Synthetisches `PART` beim Aktivieren von `+X`

Angenommen, ein IRC-Operator befindet sich bereits in mehreren Kanälen und aktiviert anschließend `+X`.

Nur zukünftige JOIN-Ereignisse zu filtern, würde nicht ausreichen: Normale Benutzer haben diesen IRC-Operator bereits in ihren lokalen Nicklisten.

Deshalb durchläuft das Modul die bestehenden Kanalmitgliedschaften des IRC-Operators und sendet betroffenen lokalen Benutzern ein gewöhnlich wirkendes synthetisches `PART`:

```text
:OperNick!user@host PART #channel
```

Absichtlich wird kein besonderer PART-Grund angegeben.

Aus Sicht eines normalen IRC-Clients hat der IRC-Operator den Kanal einfach verlassen.

In UnrealIRCd findet kein echtes PART statt.

IRC-Operatoren erhalten dieses synthetische Ereignis nicht.

## Synthetisches `JOIN` beim Deaktivieren von `+X`

Wird `+X` entfernt, geschieht das Umgekehrte.

Normale Benutzer gingen bisher davon aus, dass der IRC-Operator den Kanal verlassen hatte. Deshalb sendet das Modul ihnen ein synthetisches JOIN.

Gegebenenfalls stellt das Modul auch die Mitgliedschaftsmodi des Benutzers wieder her, damit der lokale Zustand des empfangenden Clients wieder dem sichtbaren Zustand entspricht.

Auch hier hat der IRC-Operator den Kanal auf dem Server nie tatsächlich verlassen oder erneut betreten.

## Der Filter für ausgehende Pakete

Das Herzstück der Implementierung ist ein UnrealIRCd-Hook vom Typ `HOOKTYPE_PACKET`.

Anstatt jedes interne Subsystem zu ändern, das Kanalmitgliedschaften offenlegen könnte, behandelt das Modul den ausgehenden IRC-Protokollverkehr als letzte Sichtbarkeitsgrenze für lokale Benutzer ohne IRC-Operatorstatus.

Der Paket-Hook kann:

1. die ausgehende IRC-Zeile untersuchen;
2. ihre Quelle und ihren Befehl bestimmen;
3. feststellen, ob ein Benutzer mit `+X` offengelegt würde;
4. die Zeile vollständig unterdrücken oder umschreiben, wenn nur ein Teil der Antwort verborgen werden muss.

So entstehen zwei Ansichten desselben wahrheitsgemäßen Serverzustands:

```text
                         Tatsächlicher Kanalzustand
                                |
                         UnrealIRCd-Kern
                                |
                         Ausgehende Nachricht
                                |
                    Paketfilter für Unsichtbarkeit
                         /              \\
                        /                \\
                       v                  v
                IRC-Operator       normaler Benutzer
                 unverändert          gefiltert
```

Für synthetische Übergänge, die das Modul selbst erzeugt, wird eine kleine interne Umgehungslogik verwendet. So werden diese Übergänge nicht versehentlich vom eigenen Filter abgefangen.

## NAMES-Filterung

`/NAMES` wird nicht dadurch behandelt, dass der Benutzer in UnrealIRCd als abwesend gilt.

Stattdessen schreibt das Modul die ausgehende Antwort `RPL_NAMREPLY` (`353`) für lokale Benutzer ohne IRC-Operatorstatus um.

Der Parser berücksichtigt IRC-Protokolldetails wie:

- IRCv3-Nachrichtentags;
- Nick-Präfixe;
- Kanalmitgliedschaftspräfixe;
- multi-prefix;
- Einträge im userhost-in-names-Format.

Einträge zu IRC-Operatoren mit `+X` werden entfernt, der Rest der Antwort bleibt erhalten.

IRC-Operatoren erhalten die ursprüngliche Antwort.

## WHO- und WHOX-Filterung

Standard-WHO-Antworten (`352`) und WHOX-Antworten (`354`) werden geprüft, bevor sie an lokale Benutzer ohne IRC-Operatorstatus gesendet werden.

Würde eine Antwort einen IRC-Operator mit `+X` offenlegen, wird die entsprechende Zeile unterdrückt.

WHOX ist weniger starr als herkömmliches WHO, da Clients unterschiedliche Feldanordnungen anfordern können. Deshalb muss die Implementierung den in einer WHOX-Antwort dargestellten Nick besonders sorgfältig bestimmen.

## Kanalsichtbarkeit in WHOIS

Kanalmitgliedschaften können auch über WHOIS offengelegt werden.

Das Modul verwendet für WHOIS den UnrealIRCd-Hook zur Kanalsichtbarkeit. So kann ein Benutzer ohne IRC-Operatorstatus die NAMES- und WHO-Filterung nicht einfach umgehen, indem er WHOIS-Informationen des verborgenen Operators abfragt.

IRC-Operatoren erhalten weiterhin die tatsächliche Kanalliste.

## JOIN, PART und QUIT

Normale Mitgliedschaftsereignisse eines IRC-Operators mit `+X` werden für lokale Benutzer ohne IRC-Operatorstatus unterdrückt.

Dadurch entsteht die gewünschte Illusion:

```text
IRCOp aktiviert +X
        |
normaler Benutzer erhält ein synthetisches PART
        |
IRCOp kann Kanäle tatsächlich verlassen oder betreten
        |
diese Mitgliedschaftsereignisse bleiben verborgen
        |
IRCOp deaktiviert +X
        |
normaler Benutzer erhält ein synthetisches JOIN
```

Der normale Benutzer erhält dadurch keine widersprüchlichen Mitgliedschaftsereignisse, solange der Operator unsichtbar sein soll.

## `PRIVMSG` und `NOTICE` bleiben absichtlich sichtbar

Kanalnachrichten werden **nicht** verborgen.

Ein IRC-Operator mit `+X` kann normal sprechen:

```text
<OperNick> hello
```

Ein normaler Benutzer kann diese Nachricht sehen, obwohl `OperNick` nicht als Kanalmitglied angezeigt wird.

Wichtig ist: Das Sprechen verändert weder den tatsächlichen Mitgliedschaftszustand noch macht es den Operator dauerhaft über NAMES oder WHO sichtbar.

Dieses Verhalten ist beabsichtigt und einer der Gründe, warum das Modul eine Mitgliedschaftsmarkierung nach Art von delayed join nicht als primären Mechanismus verwendet.

## MODE-Filterung

Kanal-MODE-Nachrichten erfordern eine besondere Behandlung, da eine Modusänderung einen verborgenen Nick erwähnen kann.

Eine komplette MODE-Zeile zu verwerfen wäre falsch, wenn dieselbe Zeile weitere, unabhängige Modusänderungen enthält.

Deshalb analysiert das Modul kombinierte MODE-Operationen und entfernt nur Mitgliedschaftsmodi (`q/a/o/h/v`), die auf verborgene Benutzer mit `+X` zielen. Nicht betroffene Operationen und ihre Parameter bleiben erhalten.

So soll verhindert werden, dass eine verborgene Mitgliedschaftsänderung bei allen anderen zu einem Zustandsverlust führt, nur weil sie im selben MODE-Befehl steht.

Der Parser kennt das Parameterverhalten gängiger UnrealIRCd-6-Kanalmodi. Drittanbietermodule können zusätzliche Parameterregeln für Kanalmodi einführen, die diesem Modul nicht bekannt sind.

## KICK und andere mitgliedschaftsbezogene Ereignisse

Ausgehende KICKs und andere Ereignisse einer Quelle werden geprüft, damit sie einen verborgenen IRC-Operator nicht ohne Weiteres über den normalen Kanalverkehr offenlegen.

Die aktuelle Implementierung konzentriert sich vor allem darauf, **was der Server an den Betrachter sendet**.

Sie versucht nicht, die Semantik aller Befehle auf Serverseite in UnrealIRCd neu zu definieren.

---

# Bekannte Einschränkungen und Designentscheidungen

## `/LIST` zeigt weiterhin die tatsächliche Benutzerzahl

Das Modul verfälscht derzeit nicht die Kanalbenutzerzahlen, die `/LIST` zurückgibt.

Ein Kanal kann daher zwei Benutzer melden, obwohl ein normaler Benutzer nur ein sichtbares Mitglied identifizieren kann.

Das ist vorerst beabsichtigt.

Das Ziel des Moduls ist, **Identität und Mitgliedschaft** des IRC-Operators zu verbergen, nicht für jeden Betrachter ein mathematisch vollständig isoliertes Paralleluniversum zu schaffen.

## Auskundschaften über Befehle

Da die zentrale Sichtbarkeitsbarriere den ausgehenden Protokollverkehr filtert, können Befehle wie `INVITE` oder `KICK` einem ausreichend entschlossenen Kanaloperator weiterhin ermöglichen, anhand des Befehlsverhaltens oder numerischer Antworten auf ein unsichtbares Mitglied zu schließen.

UnrealIRCd bietet APIs zum Überschreiben von Befehlen, mit denen solche Auskundschaftungsversuche umfassender verhindert werden könnten.

Das liegt derzeit außerhalb des Hauptumfangs dieser Implementierung.

## Kanalmodi von Drittanbietern

Der MODE-Umschreiber versteht die Parameterregeln, die für standardmäßige und gängige Kanalmodi von UnrealIRCd 6 erforderlich sind.

Ein Drittanbietermodul, das ungewöhnliche parametrisierte Kanalmodi hinzufügt, kann eine Erweiterung des MODE-Parsers erforderlich machen.

## Netzwerke mit mehreren Servern

`+X` ist als globaler Benutzermodus registriert.

Dieses Projekt wurde jedoch zunächst in einer kleinen UnrealIRCd-6.2.7-Testumgebung entwickelt und getestet. Komplexe Szenarien mit mehreren Servern, Netbursts, Interaktionen mit Diensten und alle möglichen S2S-Übergänge wurden **noch nicht** umfassend überprüft.

Teste das Modul in einem echten Netzwerk auf jeder Serverrolle und stelle sicher, dass es überall konsistent eingesetzt wird.

## IRC-Clients

Das Modul arbeitet auf IRC-Protokollebene. IRC-Clients unterscheiden sich jedoch darin, wie sie numerische Antworten darstellen und ihre grafischen Nicklisten verwalten.

Bei Tests sollte deshalb zwischen dem tatsächlichen Serververhalten des Protokolls und clientspezifischen Darstellungsbesonderheiten unterschieden werden.

---

# Aktuelle Tests

Die ursprüngliche Entwicklungsumgebung verwendet UnrealIRCd **6.2.7**.

Folgendes Verhalten wurde während der Entwicklung manuell getestet:

- Das Modul lässt sich als UnrealIRCd-Drittanbietermodul kompilieren.
- Das Modul wird erfolgreich geladen.
- Ein IRC-Operator kann `+X` aktivieren.
- Beim Aktivieren von `+X` verschwindet der Operator wie erwartet für normale Benutzer.
- Beim Deaktivieren von `+X` wird ein synthetisches JOIN gesendet und der Operator erscheint wieder.
- Die NAMES-Filterung verbirgt den IRC-Operator mit `+X`.
- Die WHO-Filterung verbirgt den IRC-Operator mit `+X`.
- Der verborgene IRC-Operator kann weiterhin im Kanal sprechen.
- Das Sprechen macht die Mitgliedschaft nicht dauerhaft sichtbar.
- `/LIST` behält absichtlich die tatsächliche Kanalbenutzerzahl bei.

Diese Software ist weiterhin experimentell. Vor einem produktiven Einsatz sollten weitere Sonderfälle getestet werden.

---

# Kompatibilität

Derzeit ausgerichtet auf:

- UnrealIRCd 6;
- manuell mit UnrealIRCd 6.2.7 getestet;
- *NIX-Buildumgebung.

Andere UnrealIRCd-6.x-Versionen können funktionieren, aber API und Protokollverhalten sollten getestet und nicht einfach vorausgesetzt werden.

---

# Lizenz

`unrealircd-invisibility` wird unter der **GNU General Public License Version 2.0 oder höher** (`GPL-2.0-or-later`) veröffentlicht.

Auch UnrealIRCd wird unter GPLv2 oder höher verbreitet.

Dieses Projekt ist ein unabhängiges Drittanbietermodul und **kein** offizielles UnrealIRCd-Modul.

---

# Entwicklungshinweis

Dieses Modul entstand als persönliches Wiederbelebungsprojekt mit Unterstützung von **Codex**.

Der ursprüngliche Antrieb war weniger, eine moderne IRC-Funktion hinzuzufügen, sondern vielmehr ein klassisches UnrealIRCd-Verhalten zurückzubringen, an das sich manche langjährigen IRC-Benutzer vielleicht erinnern. Gleichzeitig sollte ausgelotet werden, wie weit eine KI-gestützte Implementierung mit der modernen UnrealIRCd-6-Modul-API kommen kann.

Fehlermeldungen, Testergebnisse und Verbesserungsvorschläge sind willkommen.