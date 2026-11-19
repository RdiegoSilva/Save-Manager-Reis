# SAVE MANAGER REIS — homebrew para PS2

Gerenciador de saves com interface estilo dashboard.

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
- **Som: Ligado/Desligado:** liga e desliga os efeitos sonoros.
- **Acertar data e hora:** edita dia, mês, ano, hora e minuto (Cima/Baixo altera, Esq/Dir muda o campo, X grava, O cancela) e grava no relógio do console. A hora é a do fuso configurado no PS2. Os backups usam essa data no nome.
- **Trocar ícone do save:** troca a imagem do ícone 3D do save por uma sua. Veja a seção abaixo.
- **Backup de tudo para USB:** pergunta o formato (Pasta, PSU ou MAX) e exporta todos os saves nele.
- **Recuperar cartão ilegível / corrompido:** tenta ler o cartão 4 vezes. Se continuar ilegível, oferece formatar.
- **Formatar Memory Card:** oferece backup em PSU no USB, pede confirmação duas vezes e então formata o slot atual.

Limite: o app não conserta o sistema de arquivos do cartão. Se o cartão estiver ilegível, os saves dele não podem ser lidos aqui, e a saída é formatar. Se o cartão ainda lê, faça o backup antes de qualquer outra coisa.

## Ícone personalizado

Formato da imagem: **BMP, 128 × 128 pixels, 24 bits, sem compressão** (sem transparência).
Qualquer editor exporta isso (no Paint: Salvar como → BMP de 24 bits; no GIMP: Exportar como .bmp, "Não gravar informações de espaço de cores").

1. Coloque o `.bmp` em `mass:/SaveManagerReis/icones/` (a pasta é criada na primeira vez que você abre a opção).
2. Na aba MEMORY CARD, selecione o save (ou marque só um) e aperte **START → Trocar icone do save**.
3. Escolha a imagem e confirme.

O app troca a textura do ícone (o modelo 3D continua o mesmo) nos arquivos de ícone citados no `icon.sys`. O arquivo original é guardado antes em `SaveManagerReis/icones/originais/<SAVE>/` (só na primeira troca), então dá para restaurar copiando de volta para o save.

## Som

Efeitos sonoros gerados pelo próprio programa (sem arquivos de áudio): clique ao mover o cursor, ao marcar, ao trocar de aba, ao confirmar/voltar, aviso de sucesso, aviso de erro e um som de abertura. Para desligar: START → **Som: Ligado/Desligado**.

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
- Importar `.cbs` (CodeBreaker), `.xps` / `.sps` (SharkPort/Xploder).
- Ícone 3D também para saves em PSU / MAX.
- Backup completo do cartão (imagem inteira).
