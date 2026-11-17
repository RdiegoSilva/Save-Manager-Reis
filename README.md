# SAVE MANAGER REIS — homebrew para PS2

Gerenciador de saves com interface estilo dashboard.

## O que faz

- Lista os saves do **Memory Card** (slot 1 / slot 2) em cards organizados:
  ícone por região (US / EU / JP / KR), nome da pasta, tamanho, data e hora.
- **Exporta** Memory Card → pendrive / HD USB.
- **Importa** pendrive / HD USB → Memory Card (pergunta antes de sobrescrever).
- Relógio do console no topo, barra de progresso, avisos de sucesso/falha.
- Aba **REDE (SMB)**: reservada, ainda não implementada.

## Como os backups ficam organizados no pendrive

```
mass:/SaveManagerReis/
 └─ BASLUS-20552GTA3/
     ├─ 2026-11-16_21-18-05/   <- cada exportação cria uma pasta com data/hora
     │    ├─ icon.sys
     │    ├─ gta3.ico
     │    └─ ...arquivos do save
     └─ 2026-11-20_10-02-41/
```

Exportar o mesmo save várias vezes nunca sobrescreve backups antigos.

## Controles

| Botão | Ação |
|---|---|
| D-pad | Navegar entre os cards |
| X | Marcar / desmarcar save |
| Quadrado | Marcar / desmarcar todos |
| Triângulo | Exportar (aba Memory Card) / Importar (aba USB) |
| Círculo | Atualizar a lista |
| L1 / R1 | Trocar de aba |
| SELECT | Alternar slot 1 / slot 2 do Memory Card |
| START | Sair |

Sem nenhum card marcado, a ação vale para o card selecionado.

## Compilar o ELF

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
- Backup completo do cartão e importação de formatos `.psu` / `.max`.
