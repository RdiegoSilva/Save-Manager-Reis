# SAVE MANAGER REIS — homebrew para PS2

Criado por **Diego Reis**.

Gerenciador de saves com interface estilo dashboard.

A logo (um Memory Card com uma coroa) aparece na tela de abertura e no cabeçalho; é desenhada pelo próprio programa, sem arquivo de imagem.

## O que faz

- Lista os saves do **Memory Card** (slot 1 / slot 2, ou todos os cartões de um **multitap**: 1A–1D e 2A–2D) em cards organizados:
  ícone por região (US / EU / JP / KR), nome da pasta, tamanho, data e hora.
- **Exporta** Memory Card → pendrive / HD USB.
- **Importa** pendrive / HD USB → Memory Card (pergunta antes de sobrescrever).
- **Converte direto** nos formatos **PSU** e **MAX** (exportar e importar), além da pasta normal.
- Mostra o **nome do jogo** (título do `icon.sys`) no card, com a pasta embaixo. Funciona em Memory Card, backups em Pasta e PSU; em MAX (compactado) e em títulos só em japonês aparece o nome da pasta.
- Ao importar um save que **já existe** no cartão, o aviso compara data/hora e tamanho do cartão e do backup e diz se o cartão foi alterado depois do backup.
- Espaço livre do Memory Card no quadro **LIVRE** (amarelo abaixo de 1 MB, vermelho abaixo de 256 KB); se faltar espaço ao importar, o aviso mostra quanto precisa e quanto há livre.
- Relógio do console no topo, barra de progresso, avisos de sucesso/falha.
- **Ferramentas do Memory Card** (START): verificar saves, apagar, backup completo, recuperar cartão ilegível e formatar.
- **Visão em lista ou grade** (**L3**): alterna entre os cards em grade e uma lista de uma linha por save (ícone, nome, pasta, formato, tamanho e data). Na lista, Esq/Dir pulam uma página. A escolha fica salva no `config.ini`.
- **Ordenar, filtrar e buscar** (START → Organizar): ordem por Nome, Data, Tamanho ou Região; filtro por região (US, EU, JP, KR, Outras, PS1); busca por nome da pasta ou do jogo. Com filtro ativo, o quadro de saves mostra `exibidos/total`.
- **Tutorial** (START → Tutorial): tela de ajuda com 3 páginas explicando cada botão, atalho, aba, formato e item do menu.
- **Detalhes do save** (R3): título, pasta, formato, região, data, tamanho e lista de arquivos com o tamanho de cada um, com o **ícone do jogo** ao lado do título (o ícone 3D girando, quando o save tem; senão o ícone da região) (Memory Card, backups em Pasta e PSU).
- **Importa também `.cbs` (CodeBreaker), `.sps` (SharkPort) e `.xps` (X-Port).**
- **Cartões virtuais (VMC do OPL):** os saves de dentro dos arquivos `.bin` / `.ps2` / `.vmc` em `mass:/VMC/` aparecem na aba USB com a etiqueta **VMC**. Dá para importar para o Memory Card real e converter para Pasta, PSU ou MAX.
- **Saves de PS1** no Memory Card aparecem com a etiqueta **PS1** (roxa), entram no backup e **podem ser restaurados** (a marca de save de PS1 é recriada; veja a seção abaixo).
- **Renomear** (START → Renomear): troca o **nome do jogo** (o título que o PS2 mostra), o nome da pasta do save ou o nome do backup no USB.
- **Recuperar save selecionado** (START): tenta ler o save com várias tentativas, grava uma cópia segura no USB e, se houve erro de leitura, oferece reescrever o save no cartão.
- **Cancelar operações longas:** em exportar, importar, converter, copiar entre cartões, verificar e criar VMC, **segure O**. O app termina o save em andamento e para (nada fica pela metade).
- **Criar cartão virtual novo (VMC)** de 8, 16 ou 32 MB, direto em `mass:/VMC/`.
- **Configurações salvas** em `mass:/SaveManagerReis/config.ini`: som, ordem da lista, formatos de exportação e conversão, slot e aba.
- Aba **CONVERTER**: Pasta ⇄ PSU ⇄ MAX direto no pendrive.

## Como os backups ficam organizados no pendrive

```
mass:/SaveManagerReis/
 └─ BASLUS-20552GTA3/
     ├─ 2026-11-16_21-18-05/        <- formato Pasta: arquivos soltos do save
     │    ├─ icon.sys
     │    ├─ gta3.ico
     │    └─ ...arquivos do save
     ├─ 2026-11-20_10-02-41.psu     <- formato PSU
     └─ 2026-11-20_10-02-41.max     <- formato MAX
```

Os arquivos `.psu` e `.max` têm a data/hora no nome, então exportar várias vezes
nunca sobrescreve backups antigos (exceto dentro do mesmo segundo).

## Formatos de save (PSU e MAX)

| Formato | Origem / uso | Observação |
|---|---|---|
| **Pasta** | padrão do Save Manager Reis | arquivos soltos, sem compactação |
| **PSU** | EMS, uLaunchELF, mymc, PCSX2 | sem compactação, o mais compatível |
| **MAX** | Action Replay MAX (`Ps2PowerSave`) | compactado (LZARI) + CRC32 |
| **VMC** | cartão virtual do OPL (`mass:/VMC/*.bin`) | só leitura: importa e converte os saves de dentro |
| **CBS** | CodeBreaker | só importação e conversão (compactado + criptografado) |
| **SPS / XPS** | SharkPort / X-Port | só importação e conversão |

- **Exportar:** na aba MEMORY CARD, **R2** troca o formato (Pasta → PSU → MAX) e
  **Triângulo** exporta nele.
- **Importar:** na aba USB aparecem os backups em qualquer um dos formatos
  (a etiqueta PSU / MAX aparece no card). Para importar um save baixado da
  internet, basta colocar o `.psu` ou `.max` dentro de `mass:/SaveManagerReis/`
  (solto ou dentro de uma subpasta) e apertar **O** para atualizar.
- **Aba CONVERTER:** converte entre **Pasta, PSU e MAX** em qualquer direção, só no pendrive (sem Memory Card). Marque os backups com **X**, escolha o destino com **R2** e aperte **Triângulo**. Backups que já estão no formato de destino são ignorados. O resultado fica ao lado do original, em `SaveManagerReis/<SAVE>/<nome>.psu`, `.max` ou pasta `<nome>/`.
- O nome do save é lido de dentro do arquivo, não do nome do arquivo.
- Saves em PSU/MAX mostram o ícone da região (o ícone 3D só aparece nos backups em Pasta).
- Limite: um `.max` descomprimido pode ter até 24 MB.

Exportar o mesmo save várias vezes nunca sobrescreve backups antigos.

## Controles

| Botão | Ação |
|---|---|
| D-pad | Navegar entre os cards |
| X | Marcar / desmarcar save |
| Quadrado | Marcar / desmarcar todos |
| Triângulo | Exportar (aba Memory Card) / Importar (aba USB) |
| Círculo | Atualizar a lista |
| O (segurado) | Cancela exportar, importar, converter, copiar, verificar e criar VMC |
| L3 | Alternar grade / lista |
| R3 | Detalhes do save selecionado |
| L1 / R1 | Trocar de aba (Memory Card / USB / Converter) |
| SELECT | Atualizar tudo (reler Memory Card e USB) |
| L2 | Trocar de Memory Card: slot 1 / 2, ou 1A–1D e 2A–2D com multitap |
| R2 | Escolher o formato: exportação (aba Memory Card) ou destino (aba Converter) |
| START | Menu de ferramentas (inclui Sair) |

Sem nenhum card marcado, a ação vale para o card selecionado.

## Ferramentas do Memory Card

Aperte **START** em qualquer aba. Funciona também com cartão não formatado. O menu também tem **Sair do aplicativo**.

- **Verificar saves:** lê todos os arquivos de cada save e confere se existe `icon.sys`. Lista os que têm problema e oferece apagá-los.
- **Apagar saves marcados:** apaga os marcados (ou o card selecionado). Oferece backup antes.
- **Copiar marcados para o outro Memory Card:** copia os saves marcados (ou o selecionado) do slot atual para outro slot, direto, sem passar pelo USB. Com multitap, pergunta qual slot é o destino. Avisa se o save já existe e confere o espaço livre.
- **Créditos:** mostra a versão, o desenvolvedor (Diego Reis) e o nome da música (HilalDeep - All for Music (Original Mix)). Também fica dentro de Ajustes.
- **Ajustes:** submenu com Som (liga e desliga os efeitos), **Modo de vídeo**, Acertar data e hora e Créditos (versão, autor Diego Reis e música: HilalDeep - All for Music (Original Mix)).
- **Modo de vídeo:** veja a seção abaixo.
- **Acertar data e hora** (dentro de Ajustes): edita dia, mês, ano, hora e minuto (Cima/Baixo altera, Esq/Dir muda o campo, X grava, O cancela) e grava no relógio do console. A hora é a do fuso configurado no PS2. Os backups usam essa data no nome.
- **Trocar ícone do save:** troca a imagem do ícone 3D do save por uma sua. Veja a seção abaixo.
- **Backup de tudo para USB:** pergunta o formato (Pasta, PSU ou MAX) e exporta todos os saves nele.
- **Recuperar cartão ilegível / corrompido:** tenta ler o cartão 4 vezes. Se continuar ilegível, oferece formatar.
- **Formatar Memory Card:** oferece backup em PSU no USB, pede confirmação duas vezes e então formata o slot atual.

Limite: o app não conserta o sistema de arquivos do cartão. Se o cartão estiver ilegível, os saves dele não podem ser lidos aqui, e a saída é formatar. Se o cartão ainda lê, faça o backup antes de qualquer outra coisa.

## Cartões virtuais (VMC)

O OPL guarda os cartões virtuais em `VMC/` no pendrive (por exemplo `mass:/VMC/SLUS_200.52_0.bin`). O app lê o sistema de arquivos do cartão por dentro (com ou sem bytes de ECC) e lista cada save como um backup na aba **USB**, com a etiqueta **VMC** e o nome do arquivo do cartão. Use **Triângulo** para importar para o Memory Card real, ou a aba **CONVERTER** para gerar Pasta, PSU ou MAX.

### Criar um VMC novo

START → **Criar cartão virtual novo (VMC)**: escolha 8, 16 ou 32 MB, digite o nome (letras, números, `_`, `-` e `.`; sem a extensão) e confirme. O arquivo é criado em `mass:/VMC/NOME.bin` já formatado, sem ECC e com páginas de 512 bytes, que é o formato que o OPL usa. A gravação mostra uma barra de progresso (32 MB em USB pode levar alguns minutos) e **segurar O cancela** e apaga o arquivo incompleto. Se já existir um VMC com o nome, o app não sobrescreve. Depois, escolha esse arquivo nas configurações do jogo no OPL.

Limites desta versão: só **leitura** (não grava saves dentro do VMC), só a pasta `mass:/VMC/` e só cartões no formato padrão (páginas de 512 bytes, clusters de 1 KB). Se o OPL estiver em outro dispositivo (rede ou HD interno), copie o `.bin` para `mass:/VMC/`.

## Saves de PS1

Saves de PS1 gravados num Memory Card de PS2 aparecem com a etiqueta **PS1**, entram no filtro de região (**PS1**) e são exportados e importados como os demais (Pasta ou PSU; em Pasta o app grava um arquivo marcador `SMR_PS1.FLG` junto do backup, e em PSU a marca fica no cabeçalho da pasta). O formato MAX não guarda essa marca.

Ao **importar ou copiar entre cartões**, o app recria a marca de "save de PS1" da pasta (atributo `0x9027`) com `mcSetFileInfo`, o mesmo método do uLaunchELF. **Esse passo ainda não foi testado num console real**: faça o primeiro teste com um save de PS1 que você possa perder e confira no navegador do PS2 se ele aparece como save de PS1. A verificação de saves ignora os de PS1, pois não têm `icon.sys`.

## Renomear

START → **Renomear (nome do jogo, pasta ou backup)** abre três escolhas:

**1. Nome do jogo (título mostrado no PS2).** Troca o título que o navegador do PS2 mostra para o save, que fica dentro do `icon.sys`. São **duas linhas de até 16 caracteres** (a segunda pode ficar vazia), com letras, números e pontuação comum; o app grava em Shift-JIS de largura total, como os saves oficiais. O app mostra "Antes / Depois" e pede confirmação. Funciona em:
- **Memory Card:** reescreve o `icon.sys`; se a gravação falhar, o original é restaurado.
- **Backup em Pasta e em PSU:** troca só os 70 bytes do título no lugar, sem mudar o tamanho do arquivo nem tocar no resto.
- Outros formatos (MAX, CBS, SPS, XPS, VMC): importe no cartão, renomeie e exporte de novo.
- Saves de PS1 não têm título editável. Se o título original estiver só em japonês (kanji ou kana), o campo abre vazio e o nome novo substitui o antigo por completo.
O nome do jogo **não afeta** o funcionamento: o jogo continua achando o save normalmente.

**2. Nome da pasta do save (técnico).** No Memory Card, troca o nome da pasta (até 31 caracteres). **Atenção:** o jogo procura o save pelo nome da pasta (por exemplo `BASLUS-20552GTA3`); com outro nome ele provavelmente não vai achar o save. O app avisa antes e confere depois se o cartão aceitou.

**3. Nome do arquivo do backup (USB).** Troca o nome do arquivo ou pasta do backup, sem mexer no save de dentro. Os backups criados pelo app começam com a data (`2026-11-16_21-18-05`); deixe a data no começo e acrescente o que quiser depois (`_GTA100`) e a data continua aparecendo no card. Cartões virtuais (VMC) não são renomeados por aqui.

## Recuperar save selecionado

Na aba Memory Card, selecione o save e use START → **Recuperar save selecionado (corrompido)**:

1. Lê cada arquivo do save; se der erro, tenta de novo (3 vezes) e depois por pedaços de 1 KB (3 tentativas por pedaço). O que realmente não der para ler vira zeros.
2. Grava uma **cópia segura** no USB, em `SaveManagerReis/<SAVE>/<data>_RECUP`, antes de mexer em qualquer coisa.
3. Mostra o relatório: arquivos completos, parciais e ilegíveis, quanto foi perdido e se falta o `icon.sys`.
4. Se houve erro de leitura, pergunta se deve **apagar o save e gravar de novo** a partir do que foi recuperado, o que cria uma estrutura nova e limpa no cartão.

Limites: ele recupera o que o cartão ainda consegue ler e corrige a estrutura do save. Ele **não conserta o conteúdo** dos arquivos: se o jogo diz que o save está corrompido mas o cartão leu tudo sem erro, o problema está nos dados do próprio jogo (o app avisa nesse caso e não reescreve nada). Precisa de pendrive conectado, por causa da cópia segura.

## Filtro e busca

**START → Organizar / filtrar / buscar**. A busca abre o **teclado na tela** (o mesmo usado para nomes de save, backup e VMC): o direcional move o cursor, **X** digita a tecla, **Quadrado** apaga, **Triângulo** põe espaço, **L1/R1** alternam maiúsculas e minúsculas (o teclado abre em maiúsculas), **START** confirma e **O** cancela. Embaixo da grade há botões para Aa, espaço, apagar, limpar, OK e cancelar. A busca olha o nome da pasta e o nome do jogo, sem diferenciar maiúsculas. O backup de tudo ignora filtro e busca.

## Ícone personalizado

Formato da imagem: **BMP, 128 × 128 pixels, 24 bits, sem compressão** (sem transparência).
Qualquer editor exporta isso (no Paint: Salvar como → BMP de 24 bits; no GIMP: Exportar como .bmp, "Não gravar informações de espaço de cores").

1. Coloque o `.bmp` em `mass:/SaveManagerReis/icones/` (a pasta é criada na primeira vez que você abre a opção).
2. Na aba MEMORY CARD, selecione o save (ou marque só um) e aperte **START → Trocar icone do save**.
3. Escolha a imagem e confirme.

O app troca a textura do ícone (o modelo 3D continua o mesmo) nos arquivos de ícone citados no `icon.sys`. O arquivo original é guardado antes em `SaveManagerReis/icones/originais/<SAVE>/` (só na primeira troca), então dá para restaurar copiando de volta para o save.

## Música de fundo

Coloque um arquivo chamado **`musica.wav`** em `mass:/SaveManagerReis/` (no pendrive/HD USB). Ao abrir o app com o USB conectado, ele é carregado e toca em **loop**; os efeitos sonoros entram por cima, sem cortar a música.

- Formato: **WAV PCM de 8 ou 16 bits**, mono ou estéreo, qualquer taxa. O arquivo é lido inteiro para a RAM, com limite de **14 MB** (use 22 kHz para músicas mais longas: ~5 MB por minuto em 22 kHz estéreo é um bom ponto de partida).
- Converter com o ffmpeg: `ffmpeg -i minha_musica.mp3 -ar 22050 -ac 2 -c:a pcm_s16le musica.wav`
- **START → Ajustes**: liga/desliga a música, abre o **seletor de volume** (0 a 100%, em 10 passos) e recarrega o arquivo se ele foi colocado depois. A escolha fica salva no `config.ini`.
- **Seletor de volume** (Ajustes → Volume da música): barras crescentes e porcentagem na tela, com ajuste ao vivo enquanto a música toca. **Esq/Dir** ±10%, **Cima** máximo, **Baixo** mudo, **Quadrado** volta ao padrão (60%), **X** confirma e **O** cancela (volta ao valor anterior).
- Sem o arquivo, o app funciona normalmente, só com os efeitos.

### Música embutida no ELF (sem pendrive)

Coloque o `musica.wav` na **pasta do projeto**, ao lado do `Makefile`, e compile (`./build.sh`). Se o arquivo existir, o Makefile o embute no `SaveManagerReis.elf` e a música toca desde a abertura, sem precisar do pendrive. Sem o arquivo na pasta, o projeto compila normalmente, sem música embutida.

- O ELF fica maior (o tamanho do WAV é somado a ele). Mantenha o WAV abaixo de uns 10 MB.
- Se também houver um `musica.wav` em `SaveManagerReis/` no pendrive, ele **substitui** a música embutida.
- Para trocar a música embutida é preciso compilar de novo.

## Som

Efeitos sonoros gerados pelo próprio programa (sem arquivos de áudio): clique ao mover o cursor, ao marcar, ao trocar de aba, ao confirmar/voltar, aviso de sucesso, aviso de erro e um som de abertura. Para desligar: START → **Ajustes → Som**.

Usa `audsrv.irx` e `freesd.irx` do PS2SDK, embutidos no ELF (o `Makefile` já cuida disso). Os arquivos de código agora são `main.c`, `lzari.c`/`lzari.h` e `sound.c`/`sound.h`: mantenha todos na mesma pasta do `Makefile`.

## Compilar o ELF

O projeto agora tem três arquivos de código: `main.c`, `lzari.c` e `lzari.h`
(compressão do formato MAX). Mantenha os três na mesma pasta do `Makefile`.

### Opção 1 — Docker (mais fácil)

```sh
./build.sh
```

### Opção 2 — GitHub Actions (sem instalar nada)

Suba esta pasta para um repositório no GitHub. A aba **Actions** compila
automaticamente e disponibiliza `SaveManagerReis-ELF` para download.

### Opção 3 — PS2SDK instalado

```sh
export PS2DEV=/usr/local/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
make
```

Requer um PS2SDK recente (com `bdm`, `bdmfs_fatfs` e `usbmass_bd`).

## Executar

Copie `SaveManagerReis.elf` para o pendrive / Memory Card e abra com
uLaunchELF, FreeMcBoot, OPL, etc.

## Próximas etapas

- HD interno (APA/PFS).
- Ícones 3D reais dos saves (`.ico` / `.icn`) no lugar dos ícones por região.
- Escrever saves dentro de um VMC.
- Ícone 3D também para saves em PSU / MAX.
- Backup completo do cartão (imagem inteira).

## Modo de vídeo

START → **Ajustes → Modo de vídeo**. Há três opções:

| Modo | Para que serve |
|---|---|
| **Automático** (padrão) | Usa o modo que o console detecta (480i NTSC ou 576i PAL). É o ideal para **conversor AV (cabo amarelo) → HDMI**. |
| **TV de tubo (CRT)** | Mesmo vídeo entrelaçado, mas a interface é encolhida para a **área segura** (90% da tela), porque as TVs de tubo cortam as bordas. Fundo e barras continuam até a borda. |
| **TV moderna (HDMI)** | **480p progressivo**, tela cheia, sem tremulação. Precisa de **cabo componente** ou de um **adaptador HDMI feito para o PS2**; conversores de AV composto quase nunca aceitam 480p. |

Ao trocar, o app mostra "Manter este modo?" com contagem de 10 segundos: **X** mantém, **O** volta. Se a tela ficar preta, é só esperar: ele volta sozinho ao modo anterior. O modo escolhido é gravado no `config.ini` e aplicado na abertura (quando o pendrive é detectado). **Se a TV não mostrar o modo salvo, abra o app segurando L2 + R2:** o modo de vídeo salvo é ignorado e o app abre no Automático.

## Configurações

O app grava `mass:/SaveManagerReis/config.ini` sozinho, cerca de 1,5 segundo depois de você mudar algo, e lê o arquivo ao abrir (se o pendrive estiver conectado). Guarda: som, ordem da lista, formato de exportação, formato de conversão, slot do Memory Card, aba, modo de vídeo e visão (grade/lista). Filtro e busca **não** são guardados (para a lista não abrir "vazia"). Para voltar ao padrão, apague o arquivo.
