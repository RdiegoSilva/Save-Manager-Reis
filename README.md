# SAVE MANAGER REIS — homebrew para PS2

Gerenciador de saves com interface estilo dashboard.

## O que faz

- Lista os saves do **Memory Card** (slot 1 / slot 2) em cards organizados:
  ícone por região (US / EU / JP / KR), nome da pasta, tamanho, data e hora.
- **Exporta** Memory Card → pendrive / HD USB.
- **Importa** pendrive / HD USB → Memory Card (pergunta antes de sobrescrever).
- **Converte direto** nos formatos **PSU** e **MAX** (exportar e importar), além da pasta normal.
- Relógio do console no topo, barra de progresso, avisos de sucesso/falha.
- Aba **CONVERTER**: Pasta ⇄ PSU ⇄ MAX direto no pendrive.
- Aba **REDE (SMB)**: reservada, ainda não implementada.

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
| L1 / R1 | Trocar de aba (Memory Card / USB / Converter / Rede) |
| SELECT | Alternar slot 1 / slot 2 do Memory Card |
| L2 | Converter .psu/.max marcados para pasta (aba USB) |
| R2 | Escolher o formato: exportação (aba Memory Card) ou destino (aba Converter) |
| START | Sair |

Sem nenhum card marcado, a ação vale para o card selecionado.

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

- Transferência por rede (SMB).
- HD interno (APA/PFS).
- Ícones 3D reais dos saves (`.ico` / `.icn`) no lugar dos ícones por região.
- Importar `.cbs` (CodeBreaker), `.xps` / `.sps` (SharkPort/Xploder).
- Ícone 3D também para saves em PSU / MAX.
- Backup completo do cartão (imagem inteira).
