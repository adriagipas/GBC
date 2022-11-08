import GBC
import sys

if len(sys.argv)!=2:
    sys.exit('%s <ROM>'%sys.argv[0])
rom_fn= sys.argv[1]

GBC.init()
with open(rom_fn,'rb') as f:
    GBC.set_rom(f.read())
rom= GBC.get_rom()
print ( 'NBanks: %d'%rom['nbanks'] )
print ( 'Logo: '+str(rom['logo_ok']) )
print ( 'Title: '+rom['title'] )
print ( 'Manufacturer code: '+rom['manufacturer'] )
print ( 'CGB flag: '+rom['cgb_flag'] )
print ( 'License code: '+str(rom['license']) );
print ( 'SGB functions support: '+str(rom['sgb_flag']) )
print ( 'Mapper: '+rom['mapper'] )
print ( 'ROM size (16K banks): %d'%rom['rom_size'] )
print ( 'RAM size (KB): %d'%rom['ram_size'] )
print ( 'Japanese ROM: '+str(rom['japanese_rom']) )
print ( 'Version: %d'%rom['version'] )
print ( 'Checksum: %d'%rom['checksum'] )
print ( 'Checksum ok: '+str(rom['checksum_ok']) )
print ( 'Global Checksum: %d'%rom['global_checksum'] )
print ( 'Global Checksum ok: '+str(rom['global_checksum_ok']) )
GBC.loop()
GBC.close()
