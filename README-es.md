# unrealircd-invisibility

Recuperar el comportamiento histórico de invisibilidad de los IRCOp en los canales para UnrealIRCd 6, con la ayuda de Codex.

`unrealircd-invisibility` es un módulo independiente de terceros para UnrealIRCd 6 que añade un modo de usuario reservado a los operadores IRC: `+X`.

El objetivo es sencillo: reproducir el antiguo comportamiento que recuerdan muchos usuarios veteranos de UnrealIRCd, según el cual un operador IRC podía permanecer en un canal sin aparecer en la lista de miembros que ven los usuarios normales.

El módulo está diseñado deliberadamente para no modificar el estado real de las pertenencias a canales en UnrealIRCd. Así, el servidor mantiene un estado veraz y el módulo filtra lo que pueden ver los usuarios locales que no son operadores IRC.

> **Estado:** experimental, pero funcional. Desarrollado y probado inicialmente con UnrealIRCd **6.2.7** en *NIX.

Originalmente, el modo tenía asignada la letra `+I`; esta versión utiliza la mayúscula `+X`.

## Qué hace `+X`

Cuando un operador IRC activa:

```text
/MODE YourNick +X
```

los usuarios normales dejan de ver a ese operador IRC en la lista de miembros del canal.

Los operadores IRC siguen viendo el estado real.

El comportamiento previsto es el siguiente:

| Evento / comando | Usuario normal | Operador IRC |
| --- | --- | --- |
| El IRCOp entra al canal con `+X` | JOIN oculto | JOIN visible |
| El IRCOp sale del canal con `+X` | PART oculto | PART visible |
| El IRCOp se desconecta con `+X` | QUIT oculto | QUIT visible |
| `/NAMES` | IRCOp oculto | IRCOp visible |
| `/WHO` / WHOX | IRCOp oculto | IRCOp visible |
| Lista de canales en WHOIS | Oculta | Visible |
| El IRCOp envía un `PRIVMSG` / `NOTICE` al canal | Mensaje visible | Mensaje visible |
| Recuento de usuarios en `/LIST` | Recuento real | Recuento real |

Hablar en un canal **no** revela después la pertenencia oculta. Por tanto, un usuario normal puede ver un mensaje del operador IRC sin verlo en la lista de miembros del canal.

`+X` es global, está reservado a los operadores IRC y se elimina automáticamente cuando el usuario pierde el estado de operador IRC.

Si un usuario normal intenta activar `+X`, la solicitud se ignora silenciosamente porque el modo está reservado a los operadores IRC.

## ¿Por qué `+X`?

UnrealIRCd ya utiliza el modo de usuario `+I`, así que este módulo usa `+X` en su lugar.

La letra en sí no es lo importante. El propósito del módulo es recuperar el comportamiento histórico de invisibilidad de los IRCOp en los canales sin entrar en conflicto con los modos de usuario actuales de UnrealIRCd.

## Instalación

Copia el módulo en el directorio de módulos de terceros de UnrealIRCd:

```bash
cp invisibility.c /path/to/unrealircd-6.x/src/modules/third/invisibility.c
```

Desde el directorio de código fuente de UnrealIRCd:

```bash
make
make install
```

UnrealIRCd detecta automáticamente los archivos `.c` ubicados en `src/modules/third/` y los compila como módulos de terceros.

Después, añade el módulo a la configuración de UnrealIRCd:

```text
loadmodule "third/invisibility";
```

Comprueba la configuración e inicia o recarga UnrealIRCd como de costumbre.

El módulo sigue la estructura y la API estándar de módulos de terceros de UnrealIRCd 6.

## Uso

Solo un operador IRC puede activar este modo:

```text
/MODE YourNick +X
```

Para desactivarlo:

```text
/MODE YourNick -X
```

Activar o desactivar el modo **no** hace que el operador IRC salga realmente de los canales ni vuelva a entrar en ellos.

En su lugar:

- al activar `+X`, se envía un `PART` sintético de apariencia normal a los usuarios locales afectados que no son operadores IRC;
- al desactivar `+X`, se envía un `JOIN` sintético;
- la pertenencia real al canal en UnrealIRCd no cambia.

Así, los clientes IRC existentes mantienen un estado local coherente del canal sin necesitar compatibilidad con esta función en el cliente.

---

# Cómo funciona

## Principio de diseño: mantener intacta la pertenencia real

La decisión de diseño más importante es lo que el módulo **no** hace.

No elimina internamente al usuario del canal ni depende del estado de pertenencia con entrada diferida de UnrealIRCd.

Desde el punto de vista del servidor, un IRCOp con `+X` sigue siendo un miembro completamente normal del canal.

En términos conceptuales:

```text
                    Estado interno de UnrealIRCd
                             |
                             |
                    IRCOp está en #channel
                             |
               +-------------+-------------+
               |                           |
               v                           v
       Vista del operador IRC       Vista del usuario normal
               |                           |
        pertenencia real              vista filtrada
               |                           |
        IRCOp visible                 IRCOp oculto
```

Esto importa porque los privilegios del canal, la lógica del servidor, la sincronización y el tratamiento normal de las pertenencias siguen operando sobre el estado real.

La invisibilidad existe principalmente en la **capa de presentación**.

## Registro del modo de usuario

El módulo registra `+X` mediante la API de modos de usuario de UnrealIRCd:

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

Las propiedades importantes son:

- `UMODE_GLOBAL`: el modo es un modo de usuario global;
- `umode_allow_opers`: solo los operadores IRC pueden activarlo;
- `unset_on_deoper = 1`: UnrealIRCd lo elimina cuando el usuario pierde el estado de operador IRC.

El bit de modo resultante se almacena en `UMODE_INVISIBILITY` y se comprueba en todo el módulo.

## Política de visibilidad

La regla central es deliberadamente sencilla:

```text
el objetivo tiene +X
Y el destinatario no es el objetivo
Y el destinatario no es operador IRC
        =
el objetivo está oculto para ese destinatario
```

Por tanto, los operadores IRC siempre conservan una vista sin filtrar de los miembros del canal.

Esto no utiliza los privilegios del canal como límite de visibilidad. A efectos de este módulo, un usuario normal con `+v`, `+h`, `+o`, `+a` o `+q` sigue sin ser operador IRC.

## `PART` sintético al activar `+X`

Imagina que un operador IRC ya está en varios canales y después activa `+X`.

Filtrar únicamente los futuros eventos JOIN no sería suficiente: los usuarios normales ya tienen a ese operador IRC en sus listas locales de nicks.

Por eso, el módulo recorre las pertenencias actuales del operador IRC y envía a los usuarios locales afectados un `PART` sintético de apariencia normal:

```text
:OperNick!user@host PART #channel
```

Deliberadamente, el PART no incluye un motivo especial.

Desde el punto de vista de un cliente IRC normal, el operador IRC simplemente ha salido del canal.

En UnrealIRCd no se produce ningún PART real.

Los operadores IRC no reciben este evento sintético.

## `JOIN` sintético al desactivar `+X`

Al quitar `+X`, ocurre la operación inversa.

Los usuarios normales creían que el operador IRC había salido, así que el módulo les envía un JOIN sintético.

Cuando corresponde, el módulo también restaura los modos de pertenencia asociados a ese usuario para que el estado local del cliente receptor vuelva a coincidir con el estado visible.

De nuevo, el operador IRC nunca salió realmente del canal ni volvió a entrar en él en el servidor.

## El filtro de paquetes salientes

El núcleo de la implementación es un hook `HOOKTYPE_PACKET` de UnrealIRCd.

En lugar de intentar modificar todos los subsistemas internos que podrían exponer la pertenencia a un canal, el módulo trata el tráfico del protocolo IRC saliente como el último límite de visibilidad para los usuarios locales que no son operadores IRC.

El hook de paquetes puede:

1. inspeccionar la línea IRC saliente;
2. identificar su origen y comando;
3. determinar si dejaría al descubierto a un usuario con `+X`;
4. suprimir la línea por completo o reescribirla cuando solo haya que ocultar parte de la respuesta.

Así se crean dos vistas del mismo estado veraz del servidor:

```text
                         Estado real del canal
                                |
                         Núcleo de UnrealIRCd
                                |
                         Mensaje saliente
                                |
                    Hook de filtrado de invisibilidad
                         /              \\
                        /                \\
                       v                  v
                 operador IRC       usuario normal
                  sin cambios          filtrado
```

Se utiliza un pequeño mecanismo interno de bypass para las transiciones sintéticas generadas por el propio módulo, de modo que no las consuma accidentalmente su propio filtro.

## Filtrado de NAMES

`/NAMES` no se gestiona fingiendo que el usuario no existe en UnrealIRCd.

En su lugar, el módulo reescribe la respuesta saliente `RPL_NAMREPLY` (`353`) para los usuarios locales que no son operadores IRC.

El analizador tiene en cuenta detalles del protocolo IRC como:

- las etiquetas de mensaje de IRCv3;
- los prefijos de nick;
- los prefijos de pertenencia al canal;
- multi-prefix;
- las entradas con formato userhost-in-names.

Se eliminan las entradas correspondientes a los IRCOp con `+X` y se conserva el resto de la respuesta.

Los operadores IRC reciben la respuesta original.

## Filtrado de WHO y WHOX

Las respuestas WHO estándar (`352`) y WHOX (`354`) se inspeccionan antes de enviarlas a los usuarios locales que no son operadores IRC.

Si una respuesta revelaría a un IRCOp con `+X`, se suprime esa fila.

WHOX es menos rígido que el WHO tradicional porque los clientes pueden solicitar diferentes disposiciones de campos. Por eso, la implementación debe identificar con más cautela el nick representado en una respuesta WHOX.

## Visibilidad de canales en WHOIS

La pertenencia a canales también puede filtrarse mediante WHOIS.

El módulo utiliza el hook de visibilidad de canales de UnrealIRCd para WHOIS, de modo que un usuario que no sea operador IRC no pueda eludir los filtros de NAMES y WHO solicitando la información WHOIS del operador oculto.

Los operadores IRC siguen recibiendo la lista real de canales.

## JOIN, PART y QUIT

Los eventos normales de pertenencia originados por un IRCOp con `+X` se suprimen para los usuarios locales que no son operadores IRC.

Esto produce la ilusión esperada:

```text
El IRCOp activa +X
        |
el usuario normal recibe un PART sintético
        |
el IRCOp puede salir realmente de los canales o entrar en ellos
        |
esos eventos de pertenencia permanecen ocultos
        |
el IRCOp desactiva +X
        |
el usuario normal recibe un JOIN sintético
```

Así, el usuario normal no recibe eventos de pertenencia contradictorios mientras se supone que el operador es invisible.

## Los `PRIVMSG` y `NOTICE` son visibles deliberadamente

Los mensajes del canal **no** se ocultan.

Un IRCOp con `+X` puede hablar con normalidad:

```text
<OperNick> hello
```

Un usuario normal puede ver ese mensaje aunque `OperNick` no aparezca como miembro del canal.

Es importante destacar que hablar no cambia el estado real de pertenencia ni revela de forma persistente al operador mediante NAMES o WHO.

Este comportamiento es intencional y es una de las razones por las que el módulo no utiliza como mecanismo principal una marca de pertenencia de tipo delayed join.

## Filtrado de MODE

Los mensajes MODE del canal necesitan un tratamiento especial porque un cambio de modo puede mencionar un nick oculto.

Descartar una línea MODE completa sería incorrecto si esa misma línea incluye otros cambios de modo que no tienen relación.

Por tanto, el módulo analiza las operaciones MODE combinadas y elimina únicamente las operaciones de modo de pertenencia (`q/a/o/h/v`) dirigidas a usuarios ocultos con `+X`, conservando las operaciones restantes y sus parámetros.

El objetivo es evitar que un cambio de pertenencia oculto haga que los demás pierdan el estado correspondiente cuando aparece junto a otras operaciones en el mismo comando MODE.

El analizador conoce el comportamiento de los parámetros de los modos de canal comunes de UnrealIRCd 6. Los modos de canal de terceros pueden añadir otras reglas para los parámetros que este módulo desconozca.

## KICK y otros eventos relacionados con la pertenencia

Se inspeccionan los KICK salientes y otros eventos originados por una fuente para evitar que revelen fácilmente a un operador IRC oculto mediante el tráfico normal del canal.

La implementación actual se centra principalmente en **lo que el servidor envía al destinatario**.

No intenta redefinir la semántica de todos los comandos de UnrealIRCd.

---

# Limitaciones conocidas y decisiones de diseño

## `/LIST` conserva el recuento real de usuarios

Actualmente, el módulo no falsea las cifras de usuarios de los canales que devuelve `/LIST`.

Por tanto, un canal puede indicar que tiene dos usuarios mientras que un usuario normal solo puede identificar a un miembro visible.

Por ahora, esto es intencional.

El objetivo del módulo es ocultar la **identidad y pertenencia** del operador IRC, no construir un universo alternativo matemáticamente aislado para cada destinatario.

## Sondeo mediante comandos

Como el principal mecanismo de protección de la visibilidad filtra el tráfico del protocolo saliente, comandos como `INVITE` o `KICK` todavía pueden permitir que un operador de canal suficientemente decidido deduzca la existencia de un miembro no visible a partir del comportamiento del comando o de las respuestas numéricas.

UnrealIRCd ofrece API de sustitución de comandos que podrían utilizarse para tratar estos intentos de forma más exhaustiva.

Actualmente, esto queda fuera del alcance principal de esta implementación.

## Modos de canal de terceros

El reescritor MODE entiende las reglas de parámetros necesarias para los modos de canal estándar y comunes de UnrealIRCd 6.

Si un módulo de terceros añade modos de canal parametrizados poco habituales, puede ser necesario ampliar el analizador MODE.

## Redes con varios servidores

`+X` está registrado como modo de usuario global.

Sin embargo, este proyecto se desarrolló y probó inicialmente en un pequeño entorno de pruebas con UnrealIRCd 6.2.7. Los escenarios complejos con varios servidores, los netbursts, las interacciones con servicios y todas las transiciones S2S posibles **aún no** se han validado exhaustivamente.

En una red real, prueba el módulo en todos los roles de servidor y despliega la misma versión de forma coherente.

## Clientes IRC

El módulo funciona en la capa del protocolo IRC, pero los clientes IRC difieren en cómo muestran las respuestas numéricas y mantienen sus listas gráficas de nicks.

Por tanto, las pruebas deben distinguir el comportamiento real del protocolo del servidor de las particularidades de presentación de cada cliente.

---

# Pruebas actuales

El entorno de desarrollo inicial utiliza UnrealIRCd **6.2.7**.

Durante el desarrollo se han probado manualmente los siguientes comportamientos:

- el módulo se compila como módulo de terceros de UnrealIRCd;
- el módulo se carga correctamente;
- un operador IRC puede activar `+X`;
- al activar `+X`, el operador desaparece como se espera para un usuario normal;
- al desactivar `+X`, se envía un JOIN sintético y el operador vuelve a aparecer;
- el filtrado de NAMES oculta al operador con `+X`;
- el filtrado de WHO oculta al operador con `+X`;
- el operador IRC oculto puede seguir hablando en el canal;
- hablar no revela la pertenencia de forma persistente;
- `/LIST` conserva intencionalmente el recuento real de usuarios del canal.

Este software sigue siendo experimental. Antes de considerarlo listo para producción, hay que probar más casos límite.

---

# Compatibilidad

Objetivos actuales:

- UnrealIRCd 6;
- probado manualmente con UnrealIRCd 6.2.7;
- entorno de compilación *NIX.

Es posible que funcione con otras versiones 6.x de UnrealIRCd, pero conviene probar su API y el comportamiento del protocolo en vez de darlo por hecho.

---

# Licencia

`unrealircd-invisibility` se distribuye bajo la **GNU General Public License versión 2.0 o posterior** (`GPL-2.0-or-later`).

UnrealIRCd también se distribuye bajo GPLv2 o posterior.

Este proyecto es un módulo independiente de terceros y **no** es un módulo oficial de UnrealIRCd.

---

# Nota de desarrollo

Este módulo se creó como un proyecto personal de recuperación con la ayuda de **Codex**.

La motivación original no era tanto añadir una función IRC moderna como recuperar un comportamiento clásico de UnrealIRCd que quizá recuerden algunos usuarios veteranos, y comprobar hasta dónde podía llegar una implementación asistida por IA frente a la API moderna de módulos de UnrealIRCd 6.

Se agradecen los informes de problemas, los comentarios sobre las pruebas y las mejoras.