# unrealircd-invisibility

Resgatando o comportamento histórico de invisibilidade de IRCops nos canais para UnrealIRCd 6, com a ajuda do Codex.

`unrealircd-invisibility` é um módulo independente de terceiros para UnrealIRCd 6 que adiciona um modo de usuário exclusivo para operadores IRC: `+X`.

O objetivo é simples: reproduzir o comportamento antigo de que muitos usuários de longa data do UnrealIRCd se lembram, no qual um operador IRC podia permanecer em um canal sem aparecer na lista de membros vista pelos usuários comuns.

O módulo foi desenvolvido intencionalmente sem alterar o estado real de participação nos canais do UnrealIRCd. Assim, o estado mantido pelo servidor continua fiel, enquanto o módulo filtra o que os usuários locais que não são operadores IRC podem ver.

> **Status:** experimental, mas funcional. Desenvolvido e testado inicialmente com o UnrealIRCd **6.2.7** em *NIX.

Originalmente, a letra atribuída ao modo era `+I`; esta versão usa a letra maiúscula `+X`.

## O que `+X` faz

Quando um operador IRC ativa:

```text
/MODE YourNick +X
```

os usuários comuns deixam de ver esse operador IRC como membro do canal.

Os operadores IRC continuam vendo o estado real.

O comportamento esperado é:

| Evento / comando | Usuário comum | Operador IRC |
| --- | --- | --- |
| O IRCOp entra no canal com `+X` | JOIN oculto | JOIN visível |
| O IRCOp sai do canal com `+X` | PART oculto | PART visível |
| O IRCOp desconecta com `+X` | QUIT oculto | QUIT visível |
| `/NAMES` | IRCOp oculto | IRCOp visível |
| `/WHO` / WHOX | IRCOp oculto | IRCOp visível |
| Lista de canais no WHOIS | Oculta | Visível |
| IRCOp envia `PRIVMSG` / `NOTICE` ao canal | Mensagem visível | Mensagem visível |
| Contagem de usuários em `/LIST` | Contagem real | Contagem real |

Falar em um canal **não** revela depois a participação oculta. Assim, um usuário comum pode ver uma mensagem do operador IRC sem vê-lo na lista de membros do canal.

`+X` é global, restrito a operadores IRC e configurado para ser removido automaticamente quando o usuário perde o status de operador IRC.

Se um usuário comum tentar ativar `+X`, a solicitação será ignorada silenciosamente, pois o modo é reservado aos operadores IRC.

## Por que `+X`?

O UnrealIRCd moderno já usa o modo de usuário `+I`, então este módulo usa `+X`.

A letra em si não é o mais importante. O objetivo do módulo é restaurar o comportamento histórico de invisibilidade de IRCops nos canais sem conflitar com os modos de usuário atuais do UnrealIRCd.

## Instalação

Copie o módulo para o diretório de módulos de terceiros do UnrealIRCd:

```bash
cp invisibility.c /path/to/unrealircd-6.x/src/modules/third/invisibility.c
```

No diretório do código-fonte do UnrealIRCd, execute:

```bash
make
make install
```

O UnrealIRCd detecta automaticamente arquivos `.c` em `src/modules/third/` e os compila como módulos de terceiros.

Em seguida, adicione o módulo à configuração do UnrealIRCd:

```text
loadmodule "third/invisibility";
```

Verifique a configuração e inicie ou recarregue o UnrealIRCd como de costume.

O módulo segue a estrutura e a API padrão de módulos de terceiros do UnrealIRCd 6.

## Uso

Somente um operador IRC pode ativar este modo:

```text
/MODE YourNick +X
```

Para desativá-lo:

```text
/MODE YourNick -X
```

Ativar ou desativar o modo **não** faz o operador IRC realmente sair dos canais ou entrar neles novamente.

Em vez disso:

- ao ativar `+X`, um `PART` sintético com aparência normal é enviado aos usuários locais afetados que não são operadores IRC;
- ao desativar `+X`, um `JOIN` sintético é enviado;
- a participação real no canal dentro do UnrealIRCd não muda.

Isso permite que os clientes IRC existentes mantenham um estado local coerente do canal sem precisar de suporte a esse recurso no cliente.

---

# Como funciona

## Princípio de design: manter intacta a participação real

A decisão de design mais importante diz respeito ao que o módulo **não** faz.

Ele não remove o usuário do canal internamente nem depende do estado de participação com entrada atrasada do UnrealIRCd.

Do ponto de vista do servidor, um IRCOp com `+X` continua sendo um membro normal do canal.

Em termos conceituais:

```text
                    Estado interno do UnrealIRCd
                             |
                             |
                    IRCOp está em #channel
                             |
               +-------------+-------------+
               |                           |
               v                           v
      Visão do operador IRC        Visão do usuário comum
               |                           |
       participação real             visão filtrada
               |                           |
        IRCOp visível                IRCOp oculto
```

Isso é importante porque os privilégios do canal, a lógica do servidor, a sincronização e o tratamento normal das participações continuam operando sobre o estado real.

A invisibilidade existe principalmente na **camada de apresentação**.

## Registro do modo de usuário

O módulo registra `+X` por meio da API de modos de usuário do UnrealIRCd:

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

As propriedades importantes são:

- `UMODE_GLOBAL` — o modo é global;
- `umode_allow_opers` — somente operadores IRC podem ativá-lo;
- `unset_on_deoper = 1` — o UnrealIRCd remove o modo quando o usuário perde o status de operador IRC.

O bit de modo resultante é armazenado em `UMODE_INVISIBILITY` e verificado em todo o módulo.

## Política de visibilidade

A regra central é intencionalmente simples:

```text
o alvo tem +X
E o destinatário não é o alvo
E o destinatário não é operador IRC
        =
o alvo fica oculto para esse destinatário
```

Assim, os operadores IRC sempre mantêm uma visão não filtrada dos membros do canal.

Isso é diferente de usar os privilégios do canal como limite de visibilidade. Para este módulo, um usuário comum com `+v`, `+h`, `+o`, `+a` ou `+q` continua não sendo operador IRC.

## `PART` sintético ao ativar `+X`

Imagine um operador IRC que já está em vários canais e então ativa `+X`.

Filtrar apenas os próximos eventos JOIN não seria suficiente: os usuários comuns já têm esse operador IRC em suas listas locais de nicks.

Por isso, o módulo percorre as participações existentes do operador IRC e envia aos usuários locais afetados um `PART` sintético com aparência normal:

```text
:OperNick!user@host PART #channel
```

Intencionalmente, não há um motivo especial no PART.

Do ponto de vista de um cliente IRC comum, o operador IRC simplesmente saiu do canal.

Nenhum PART real ocorre dentro do UnrealIRCd.

Os operadores IRC não recebem esse evento sintético.

## `JOIN` sintético ao desativar `+X`

A operação inversa acontece quando `+X` é removido.

Os usuários comuns acreditavam que o operador IRC havia saído, então o módulo envia a eles um JOIN sintético.

Quando apropriado, o módulo também restaura os modos de participação associados ao usuário, para que o estado local do cliente receptor volte a corresponder ao estado visível.

Novamente, o operador IRC nunca saiu de verdade do canal nem entrou nele novamente no servidor.

## O filtro de pacotes de saída

O núcleo da implementação é um hook `HOOKTYPE_PACKET` do UnrealIRCd.

Em vez de tentar modificar todos os subsistemas internos que podem expor a participação em canais, o módulo trata o tráfego IRC de saída como o limite final de visibilidade para usuários locais que não são operadores IRC.

O hook de pacotes pode:

1. inspecionar a linha IRC de saída;
2. identificar sua origem e seu comando;
3. determinar se um usuário com `+X` seria exposto;
4. suprimir a linha inteira ou reescrevê-la quando apenas parte da resposta precisar ser ocultada.

Isso cria duas visões do mesmo estado verdadeiro do servidor:

```text
                         Estado real do canal
                                |
                         Núcleo do UnrealIRCd
                                |
                         Mensagem de saída
                                |
                    Hook de filtro de invisibilidade
                         /              \\
                        /                \\
                       v                  v
                 operador IRC       usuário comum
                  inalterado          filtrado
```

Um pequeno mecanismo interno de bypass é usado para as transições sintéticas geradas pelo próprio módulo, evitando que sejam interceptadas acidentalmente pelo filtro do próprio módulo.

## Filtro de NAMES

O `/NAMES` não é tratado fingindo que o usuário está ausente do UnrealIRCd.

Em vez disso, o módulo reescreve a resposta de saída `RPL_NAMREPLY` (`353`) para usuários locais que não são operadores IRC.

O analisador leva em conta detalhes do protocolo IRC, como:

- tags de mensagem do IRCv3;
- prefixos de nick;
- prefixos de participação no canal;
- multi-prefix;
- entradas no formato userhost-in-names.

As entradas correspondentes a IRCops com `+X` são removidas, enquanto o restante da resposta é preservado.

Os operadores IRC recebem a resposta original.

## Filtro de WHO e WHOX

As respostas WHO padrão (`352`) e WHOX (`354`) são inspecionadas antes de serem entregues aos usuários locais que não são operadores IRC.

Se uma resposta revelar um IRCOp com `+X`, essa linha será suprimida.

O WHOX é menos rígido que o WHO tradicional porque os clientes podem solicitar diferentes disposições de campos. Por isso, a implementação precisa ser mais cuidadosa ao identificar o nick representado em uma resposta WHOX.

## Visibilidade de canais no WHOIS

A participação em canais também pode ser revelada pelo WHOIS.

O módulo usa o hook de visibilidade de canais do UnrealIRCd para WHOIS, impedindo que um usuário que não seja operador IRC contorne os filtros de NAMES e WHO ao solicitar as informações WHOIS do operador oculto.

Os operadores IRC continuam recebendo a lista real de canais.

## JOIN, PART e QUIT

Os eventos normais de participação originados por um IRCOp com `+X` são suprimidos para usuários locais que não são operadores IRC.

Isso produz a ilusão esperada:

```text
O IRCOp ativa +X
        |
o usuário comum recebe um PART sintético
        |
o IRCOp pode realmente sair ou entrar nos canais
        |
esses eventos de participação continuam ocultos
        |
o IRCOp desativa +X
        |
o usuário comum recebe um JOIN sintético
```

Assim, o usuário comum não recebe eventos de participação contraditórios enquanto o operador deveria estar invisível.

## `PRIVMSG` e `NOTICE` são intencionalmente visíveis

As mensagens no canal **não** são ocultadas.

Um IRCOp com `+X` pode falar normalmente:

```text
<OperNick> hello
```

Um usuário comum pode ver essa mensagem mesmo que `OperNick` não apareça como membro do canal.

É importante: falar não altera o estado real de participação nem revela o operador de forma persistente por NAMES ou WHO.

Esse comportamento é intencional e é um dos motivos pelos quais o módulo não usa uma flag de participação no estilo delayed-join como mecanismo principal.

## Filtro de MODE

Mensagens MODE do canal precisam de tratamento especial porque uma alteração de modo pode mencionar um nick oculto.

Descartar uma linha MODE inteira seria incorreto quando a mesma linha contém alterações de modo sem relação com isso.

Por isso, o módulo analisa operações MODE combinadas e remove somente as operações de modo de participação (`q/a/o/h/v`) direcionadas a usuários ocultos com `+X`, preservando as demais operações e seus parâmetros.

O objetivo é evitar que uma alteração de participação oculta faça com que os outros usuários percam o estado correspondente quando ela aparece no mesmo comando MODE que outras alterações.

O analisador conhece o comportamento dos parâmetros dos modos de canal comuns do UnrealIRCd 6. Modos de canal de terceiros podem introduzir semânticas adicionais de parâmetros que este módulo desconhece.

## KICK e outros eventos relacionados à participação

KICKs de saída e outros eventos originados por uma fonte são inspecionados para que não revelem facilmente um operador IRC oculto pelo tráfego normal do canal.

A implementação atual se concentra principalmente em **o que o servidor envia ao destinatário**.

Ela não tenta redefinir a semântica de todos os comandos do UnrealIRCd.

---

# Limitações conhecidas e decisões de design

## `/LIST` mantém a contagem real de usuários

Atualmente, o módulo não falsifica os números de usuários dos canais retornados por `/LIST`.

Assim, um canal pode informar dois usuários enquanto um usuário comum consegue identificar apenas um membro visível.

Por enquanto, isso é intencional.

O objetivo do módulo é ocultar a **identidade e a participação** do operador IRC, não construir um universo alternativo matematicamente isolado para cada destinatário.

## Tentativas de sondagem por comandos

Como a principal barreira de visibilidade filtra o tráfego do protocolo de saída, comandos como `INVITE` ou `KICK` ainda podem permitir que um operador de canal suficientemente determinado deduza a existência de um membro não visível pelo comportamento do comando ou por respostas numéricas.

O UnrealIRCd oferece APIs de substituição de comandos que poderiam ser usadas para tratar essas tentativas de forma mais abrangente.

Isso está fora do escopo principal desta implementação no momento.

## Modos de canal de terceiros

O reescritor MODE entende a semântica de parâmetros necessária para os modos de canal padrão e comuns do UnrealIRCd 6.

Um módulo de terceiros que adicione modos de canal parametrizados incomuns pode exigir uma extensão do analisador MODE.

## Redes com vários servidores

`+X` é registrado como modo de usuário global.

No entanto, este projeto foi desenvolvido e testado inicialmente em um pequeno ambiente de testes com UnrealIRCd 6.2.7. Cenários complexos com vários servidores, netbursts, interações com serviços e todas as transições S2S possíveis **ainda não** foram validados de forma exaustiva.

Em uma rede real, teste o módulo em cada função de servidor e faça uma implantação consistente.

## Clientes IRC

O módulo funciona na camada do protocolo IRC, mas os clientes IRC diferem na forma como exibem respostas numéricas e mantêm suas listas gráficas de nicks.

Por isso, os testes devem distinguir o comportamento real do protocolo do servidor das particularidades de apresentação de cada cliente.

---

# Testes atuais

O ambiente inicial de desenvolvimento usa o UnrealIRCd **6.2.7**.

Os seguintes comportamentos foram testados manualmente durante o desenvolvimento:

- o módulo compila como módulo de terceiros do UnrealIRCd;
- o módulo carrega com sucesso;
- um operador IRC consegue ativar `+X`;
- ativar `+X` faz o operador desaparecer como esperado para um usuário comum;
- desativar `+X` gera um JOIN sintético e faz o operador reaparecer;
- o filtro de NAMES oculta o operador com `+X`;
- o filtro de WHO oculta o operador com `+X`;
- o operador IRC oculto ainda consegue falar no canal;
- falar não revela a participação de forma persistente;
- `/LIST` mantém intencionalmente a contagem real de usuários do canal.

Este software ainda é experimental. Mais casos extremos devem ser testados antes de considerá-lo pronto para produção.

---

# Compatibilidade

Alvos atuais:

- UnrealIRCd 6;
- testado manualmente com UnrealIRCd 6.2.7;
- ambiente de compilação *NIX.

Outras versões 6.x do UnrealIRCd podem funcionar, mas a API e o comportamento do protocolo devem ser testados, não presumidos.

---

# Licença

`unrealircd-invisibility` é distribuído sob a **GNU General Public License versão 2.0 ou posterior** (`GPL-2.0-or-later`).

O próprio UnrealIRCd também é distribuído sob a GPLv2 ou posterior.

Este projeto é um módulo independente de terceiros e **não** é um módulo oficial do UnrealIRCd.

---

# Nota de desenvolvimento

Este módulo foi criado como um projeto pessoal de resgate com a ajuda do **Codex**.

A motivação original não era tanto adicionar um recurso moderno de IRC, mas recuperar um comportamento clássico do UnrealIRCd que talvez seja lembrado por alguns usuários de longa data, além de descobrir até onde uma implementação assistida por IA poderia chegar usando a API moderna de módulos do UnrealIRCd 6.

Relatos de problemas, feedback de testes e melhorias são bem-vindos.