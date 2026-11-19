EE_BIN  = SaveManagerReis.elf

EE_OBJS = main.o lzari.o savefmt.o sound.o vmcfs.o \
          iomanX_irx.o fileXio_irx.o \
          usbd_irx.o bdm_irx.o bdmfs_fatfs_irx.o usbmass_bd_irx.o \
          freesd_irx.o audsrv_irx.o

EE_LIBS = -lgskit -ldmakit -lpad -lmtap -lmc -lcdvd -lfileXio -lpatches -laudsrv -lm

EE_CFLAGS += -O2 -Wall -DNEWLIB_PORT_AWARE

# gsKit fica fora do PS2SDK, em $(PS2DEV)/gsKit
EE_INCS    += -I$(PS2DEV)/gsKit/include
EE_LDFLAGS += -L$(PS2DEV)/gsKit/lib

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) *_irx.c

# --- IRX embutidos no ELF ---
iomanX_irx.c: $(PS2SDK)/iop/irx/iomanX.irx
	$(PS2SDK)/bin/bin2c $< $@ iomanX_irx

fileXio_irx.c: $(PS2SDK)/iop/irx/fileXio.irx
	$(PS2SDK)/bin/bin2c $< $@ fileXio_irx

usbd_irx.c: $(PS2SDK)/iop/irx/usbd.irx
	$(PS2SDK)/bin/bin2c $< $@ usbd_irx

bdm_irx.c: $(PS2SDK)/iop/irx/bdm.irx
	$(PS2SDK)/bin/bin2c $< $@ bdm_irx

bdmfs_fatfs_irx.c: $(PS2SDK)/iop/irx/bdmfs_fatfs.irx
	$(PS2SDK)/bin/bin2c $< $@ bdmfs_fatfs_irx

usbmass_bd_irx.c: $(PS2SDK)/iop/irx/usbmass_bd.irx
	$(PS2SDK)/bin/bin2c $< $@ usbmass_bd_irx

freesd_irx.c: $(PS2SDK)/iop/irx/freesd.irx
	$(PS2SDK)/bin/bin2c $< $@ freesd_irx

audsrv_irx.c: $(PS2SDK)/iop/irx/audsrv.irx
	$(PS2SDK)/bin/bin2c $< $@ audsrv_irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
