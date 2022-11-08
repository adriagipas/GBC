#!/usr/bin/env python3

from array import array
import GBC
import sys

class Inst:
    
    __BYTE_OPTS= set([GBC.BYTE, GBC.pBYTE, GBC.pFF00n])
    __DESP_OPTS= set([GBC.DESP, GBC.SPdd])
    __ADDR_WORD_OPTS= set([GBC.ADDR, GBC.WORD])
    
    __MNEMONIC= {
        GBC.UNK : 'UNK ',
        GBC.LD : 'LD  ',
        GBC.PUSH : 'PUSH',
        GBC.POP : 'POP ',
        GBC.LDI : 'LDI ',
        GBC.LDD : 'LDD ',
        GBC.ADD : 'ADD ',
        GBC.ADC : 'ADC ',
        GBC.SUB : 'SUB ',
        GBC.SBC : 'SBC ',
        GBC.AND : 'AND ',
        GBC.OR : 'OR  ',
        GBC.XOR : 'XOR ',
        GBC.CP : 'CP  ',
        GBC.INC : 'INC ',
        GBC.DEC : 'DEC ',
        GBC.DAA : 'DAA ',
        GBC.CPL : 'CPL ',
        GBC.CCF : 'CCF ',
        GBC.SCF : 'SCF ',
        GBC.NOP : 'NOP ',
        GBC.HALT : 'HALT',
        GBC.DI : 'DI  ',
        GBC.EI : 'EI  ',
        GBC.RLCA : 'RLCA',
        GBC.RLA : 'RLA ',
        GBC.RRCA : 'RRCA',
        GBC.RRA : 'RRA ',
        GBC.RLC : 'RLC ',
        GBC.RL : 'RL  ',
        GBC.RRC : 'RRC ',
        GBC.RR : 'RR  ',
        GBC.SLA : 'SLA ',
        GBC.SRA : 'SRA ',
        GBC.SRL : 'SRL ',
        GBC.RLD : 'RLD ',
        GBC.RRD : 'RRD ',
        GBC.BIT : 'BIT ',
        GBC.SET : 'SET ',
        GBC.RES : 'RES ',
        GBC.JP : 'JP  ',
        GBC.JR : 'JR  ',
        GBC.CALL : 'CALL',
        GBC.RET : 'RET ',
        GBC.RETI : 'RETI',
        GBC.RST00 : 'RST  00H',
        GBC.RST08 : 'RST  08H',
        GBC.RST10 : 'RST  10H',
        GBC.RST18 : 'RST  18H',
        GBC.RST20 : 'RST  20H',
        GBC.RST28 : 'RST  28H',
        GBC.RST30 : 'RST  30H',
        GBC.RST38 : 'RST  38H',
        GBC.STOP : 'STOP',
        GBC.SWAP : 'SWAP' }
    
    def __set_extra ( self, op, extra, i ):
        if op in Inst.__BYTE_OPTS: self.extra[i]= extra[0]
        elif op in Inst.__DESP_OPTS: self.extra[i]= extra[1]
        elif op in Inst.__ADDR_WORD_OPTS: self.extra[i]= extra[2]
        elif op == GBC.BRANCH: self.extra[i]= (extra[3][0],extra[3][1])
    
    def __init__ ( self, args ):
        self.id= args[1]
        self.bytes= args[4]
        self.addr= (args[0]-len(self.bytes))&0xFFFF
        self.op1= args[2]
        self.op2= args[3]
        self.extra= [None,None]
        self.__set_extra ( self.op1, args[5], 0 )
        self.__set_extra ( self.op2, args[6], 1 )
        
    def __str_op ( self, op, i ):
        if op == GBC.A : return ' A'
        elif op == GBC.B : return ' B'
        elif op == GBC.C : return ' C'
        elif op == GBC.D : return ' D'
        elif op == GBC.E : return ' E'
        elif op == GBC.H : return ' H'
        elif op == GBC.L : return ' L'
        elif op == GBC.BYTE : return ' %02XH'%self.extra[i]
        elif op == GBC.DESP : return ' %-d'%self.extra[i]
        elif op == GBC.SPdd : return ' SP%-d'%self.extra[i]
        elif op == GBC.pHL : return ' (HL)'
        elif op == GBC.pBC : return ' (BC)'
        elif op == GBC.pDE : return ' (DE)'
        elif op == GBC.ADDR : return ' (%04XH)'%self.extra[i]
        elif op == GBC.BC : return ' BC'
        elif op == GBC.DE : return ' DE'
        elif op == GBC.HL : return ' HL'
        elif op == GBC.SP : return ' SP'
        elif op == GBC.AF : return ' AF'
        elif op == GBC.B0 : return ' 0'
        elif op == GBC.B1 : return ' 1'
        elif op == GBC.B2 : return ' 2'
        elif op == GBC.B3 : return ' 3'
        elif op == GBC.B4 : return ' 4'
        elif op == GBC.B5 : return ' 5'
        elif op == GBC.B6 : return ' 6'
        elif op == GBC.B7 : return ' 7'
        elif op == GBC.WORD : return ' %04XH'%self.extra[i]
        elif op == GBC.F_NZ : return ' NZ'
        elif op == GBC.F_Z : return ' Z'
        elif op == GBC.F_NC : return ' NC'
        elif op == GBC.F_C : return ' C'
        elif op == GBC.BRANCH : return ' $%+d (%04XH)'%(self.extra[i][0],
                                                       self.extra[i][1])
        elif op == GBC.pB : return ' (B)'
        elif op == GBC.pC : return ' (C)'
        elif op == GBC.pD : return ' (D)'
        elif op == GBC.pE : return ' (E)'
        elif op == GBC.pH : return ' (H)'
        elif op == GBC.pL : return ' (L)'
        elif op == GBC.pA : return ' (A)'
        elif op == GBC.pBYTE : return ' (%02XH)'%self.extra[i]
        elif op == GBC.pFF00n : return ' (FF%02XH)'%self.extra[i]
        elif op == GBC.pFF00C : return ' (FF00H+C)'
        else: return str(op)
        
    def __str__ ( self ):
        ret= '%04X   '%self.addr
        for b in self.bytes: ret+= ' %02x'%b
        for n in range(len(self.bytes),4): ret+= '   '
        ret+= '    '+Inst.__MNEMONIC[self.id]
        if self.op1 != GBC.NONE :
            ret+= self.__str_op ( self.op1, 0 )
        if self.op2 != GBC.NONE :
            ret+= ','+self.__str_op ( self.op2, 1 )
        
        return ret

class Record:
    
    def __init__ ( self, inst ):
        self.inst= inst
        self.nexec= 0
        
class Tracer:
    
    MA_ROM= 0x01
    MA_VRAM= 0x02
    MA_ERAM= 0x04
    MA_RAM= 0x08
    MA_OAM= 0x10
    MA_NOT_USABLE= 0x20
    MA_IO= 0x40
    MA_HRAM= 0x80
    MA_INT= 0x100
    MA_BIOS=0x200
    
    def __init__ ( self ):
        self.next_addr= 0
        self.records= {}
        self.print_insts= False
        self.max_nexec= 0
        self.last_addr= None
        self.bank1= -1
        self.print_mapper_changed= False
        self.print_mem_access= 0
        
    def enable_print_insts ( self, enabled ):
        self.print_insts= enabled
    
    def enable_print_mapper_changed ( self, enabled ):
        self.print_mapper_changed= enabled
    
    def enable_print_mem_access ( self, mask ):
        self.print_mem_access= mask
        
    def dump_insts ( self ):
        def get_prec ( val ):
            prec= 0
            while val != 0:
                prec+= 1
                val//= 10
            return prec
        prec= get_prec ( self.max_nexec )
        code_rom= self.records.get('rom')
        if code_rom == None : return
        nbanks= len(code_rom)
        prec2= get_prec ( nbanks )
        for n in range(0,nbanks):
            code= code_rom[n]
            print ( '\n\n## BANK %d ##'%n)
            prev= True
            i= 0
            while i < 0x4000:
                rec= code[i]
                if rec == None :
                    prev= False
                    i+= 1
                else:
                    if not prev: print ( '\n' )
                    print ( ('[%'+str(prec)+'d] %0'+
                             str(prec2)+'d:%s')%(rec.nexec,n,rec.inst) )
                    prev= True
                    i+= len(rec.inst.bytes)
    
    def mapper_changed ( self ):
        self.bank1= GBC.get_bank1()
        if self.print_mapper_changed :
            print ( 'BANK1: %d'%self.bank1 )
    
    def mem_access ( self, typ, addr, data ):
        if addr < 0x900:
            if GBC.is_bios_mapped() and (addr<0x100 or addr>=0x200):
                if self.print_mem_access&Tracer.MA_BIOS == 0 : return
            else:
                 if self.print_mem_access&Tracer.MA_ROM == 0 : return
        elif addr < 0x8000 :
            if self.print_mem_access&Tracer.MA_ROM == 0 : return
        elif addr < 0xA000:
            if self.print_mem_access&Tracer.MA_VRAM == 0 : return
        elif addr < 0xC000:
            if self.print_mem_access&Tracer.MA_ERAM == 0 : return
        elif addr < 0xFE00:
            if self.print_mem_access&Tracer.MA_RAM == 0 : return
        elif addr < 0xFEA0:
            if self.print_mem_access&Tracer.MA_OAM == 0 : return
        elif addr < 0xFF00:
            if self.print_mem_access&Tracer.MA_NOT_USABLE == 0 : return
        elif addr < 0xFF80:
            if self.print_mem_access&Tracer.MA_IO == 0 : return
        elif addr < 0xFFFF:
            if self.print_mem_access&Tracer.MA_HRAM == 0 : return
        elif self.print_mem_access&Tracer.MA_INT == 0 : return
        if typ == GBC.READ :
            print ( 'MEM[%04X] -> %02X'%(addr,data) )
        else:
            print ( 'MEM[%04X]= %02X'%(addr,data) )
    
    def cpu_step ( self, *args ):
        # NOTA!!!!: Al canviar de mapper es caga tot.
        if args[0] == GBC.VBINT :
            print ( 'V-Blank INT' )
            return
        elif args[0] == GBC.LSINT :
            print ( 'LCD STAT INT' )
            return
        elif args[0] == GBC.TIINT :
            print ( 'Timer INT' )
            return
        elif args[0] == GBC.SEINT :
            print ( 'Serial INT' )
            return
        elif args[0] == GBC.JOINT :
            print ( 'Joypad INT' )
            return
        self.next_addr= args[1]
        inst= Inst ( args[1:] )
        self.last_addr= inst.addr
        if inst.addr < 0x8000 and \
                (not GBC.is_bios_mapped() or \
                      (inst.addr>=0x100 and inst.addr<0x200)):
            aux= self.records.get('rom')
            if aux == None :
                rom= GBC.get_rom()
                aux= []
                for n in range(0,rom['nbanks']):
                    aux.append ( [None]*0x4000)
                self.records['rom']= aux
            if inst.addr < 0x4000 :
                addr= inst.addr
                bank= 0
            else:
                if self.bank1 == -1 :
                    self.bank1= GBC.get_bank1()
                bank= self.bank1
                addr= inst.addr-0x4000
            aux2= aux[bank][addr]
            if aux2 == None :
                aux2= aux[bank][addr]= Record ( inst )
            aux2.nexec+= 1
            if aux2.nexec > self.max_nexec:
                self.max_nexec= aux2.nexec
        if self.print_insts:
            print ( inst )

class Color:
    
    @staticmethod
    def get ( r, g, b ):
        return (r<<16)|(g<<8)|b
    
    @staticmethod
    def get_components ( color ):
        return (color>>16,(color>>8)&0xff,color&0xff)

class Img:
    
    WHITE= Color.get ( 255, 255, 255 )
    
    def __init__ ( self, width, height ):
        self._width= width
        self._height= height
        self._v= []
        for i in range(0,height):
            self._v.append ( array('i',[Img.WHITE]*width) )
    
    def __getitem__ ( self, ind ):
        return self._v[ind]
    
    def write ( self, to ):
        if type(to) == str :
            to= open ( to, 'wt' )
        to.write ( 'P3\n ')
        to.write ( '%d %d\n'%(self._width,self._height) )
        to.write ( '255\n' )
        for r in self._v:
            for c in r:
                aux= Color.get_components ( c )
                to.write ( '%d %d %d\n'%(aux[0],aux[1],aux[2]) )

def bitplane2color ( bp0, bp1, pal4 ):
    ret= array ( 'i', [0]*8 )
    for i in range(0,8):
        ret[i]= pal4[((bp0>>7)&1)|((bp1>>6)&0x2)]
        bp0<<= 1; bp1<<= 1
    return ret

PAL4= array ( 'i', [Color.get(0,0,0),
                    Color.get(100,100,100),
                    Color.get(200,200,200),
                    Color.get(255,255,255)] )
def tiles2img ( mem, ntiles ):
    ret= Img ( 8, 8*ntiles ); j= r= 0
    for n in range(0,ntiles):
        for i in range(0,8):
            line= bitplane2color ( mem[j], mem[j+1], PAL4 )
            j+= 2
            for k in range(0,8):
                ret[r][k]= line[k]
            r+= 1
    return ret

def vramtiles2img ( vram_bank ):
    ret= Img ( 192, 128 )
    offy= 0; p= 0
    for r in range(0,16):
        offx= 0
        for c in range(0,24):
            for j in range(0,8):
                line= bitplane2color ( vram_bank[p], vram_bank[p+1], PAL4 )
                p+= 2
                r2= offy+j
                for k in range(0,8):
                    ret[r2][offx+k]= line[k]
            offx+= 8
        offy+= 8
    return ret

def cpal2pal_col ( cpal ):
    factor= 255.0/31.0
    ret= []
    for i in cpal:
        aux= []
        for j in i:
            aux.append ( Color.get ( int((j&0x1f)*factor),
                                     int(((j>>5)&0x1f)*factor),
                                     int(((j>>10)&0x1f)*factor) ) )
        ret.append ( aux )
    return ret

def map2img ( vram, pos, sel_abs, cpal= None ):
    mem= vram[0]; mem2= vram[1]
    if cpal != None : pal_col= cpal2pal_col ( cpal['bg'] )
    ret= Img ( 256, 256 )
    offy= 0; p= pos
    for r in range(0,32):
        offx= 0
        for c in range(0,32):
            b= mem[p]; b2= (mem2[p]&0x7); p+= 1
            if cpal == None : pal= PAL4
            else            : pal= pal_col[b2]
            if sel_abs : tile_p= b*16
            else       :
                if b >= 0x80 : tile_p= (0x100+b-256)*16
                else         : tile_p= (0x100+b)*16
            for j in range(0,8):
                line= bitplane2color ( mem[tile_p], mem[tile_p+1], pal )
                tile_p+= 2
                r2= offy+j
                for k in range(0,8):
                    ret[r2][offx+k]= line[k]
            offx+= 8
        offy+= 8
    return ret

def cpal2img ( cpal ):
    factor= 255.0/31.0
    ret= Img ( 320, 20 )
    offy= 0
    for r in range(0,2):
        offx= 0
        for c in range(0,32):
            pal= cpal['bg'] if r == 0 else cpal['ob']
            aux= pal[c//4][c%4]
            color= Color.get ( int((aux&0x1f)*factor),
                               int(((aux>>5)&0x1f)*factor),
                               int(((aux>>10)&0x1f)*factor) )
            for j in range(0,10):
                r2= offy+j
                for k in range(0,10):
                    ret[r2][offx+k]= color
            offx+= 10
        offy+= 10
    return ret

GBC.init(open('/home/adria/jocs/BIOS/gbc_bios.bin','rb').read())
#GBC.init()
GBC.set_rom ( open ( 'ROM.gb', 'rb' ).read() )
rom= GBC.get_rom()
#print ( rom['banks'][0][0x149] )
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

##################################
#tiles2img ( rom['banks'][28][0x34A9:], 128 ).write(open('kk.pnm','w'))
#j= 0x2815; bank= rom['banks'][61]
#for i in range(0,65):
#    print ( '%02d => %03d %04X'%(i,bank[j],(bank[j+1]|(bank[j+2]<<8))) )
#    j+= 3
#print ( [hex(b) for b in rom['banks'][16][0x1927:0x1927+8]])
t= Tracer()
#t.enable_print_insts ( True )
#t.enable_print_mapper_changed ( True )
#t.enable_print_mem_access ( Tracer.MA_ERAM|Tracer.MA_RAM|Tracer.MA_IO|Tracer.MA_HRAM|Tracer.MA_IO)
#t.enable_print_mem_access(Tracer.MA_IO)
#GBC.set_tracer ( t )
GBC.loop()
#sys.exit(0)
#GBC.trace()
#while t.last_addr != 0x01AE: GBC.trace()
#while t.last_addr == 0x22DD: GBC.trace()
#for i in range(0,400000):GBC.trace()
vram= GBC.get_vram()
cpal= GBC.get_cpal()
vramtiles2img(vram[0]).write(open('tiles.pnm','w'))
sys.exit(0)
#map2img(vram,0x1800,False,cpal).write(open('map_9800_neg.pnm','w'))
#map2img(vram,0x1C00,False,cpal).write(open('map_9C00_neg.pnm','w'))
#cpal2img(cpal).write(open('pal.pnm','w'))
#GBC.trace()
#while t.last_addr != 0x62E7 : GBC.trace()
#vramtiles2img(GBC.get_vram()[0]).write(open('tiles.pnm','w'))
#tiles2img(GBC.get_vram()[0],328).write(open('tiles.pnm','w'))
#for i in range(0,6):
#    GBC.trace()
#    while t.last_addr != 0x64CE : GBC.trace()
#    print ( GBC.get_cpal() )
#for j in range(0,10):
#    for i in range(0,10000000): GBC.trace()
#    vram= GBC.get_vram()
#    cpal= GBC.get_cpal()
#    vramtiles2img(vram[0]).write(open('tiles_%02d.pnm'%j,'w'))
#    map2img(vram,0x1800,False,cpal).write(open('map_9800_neg_%02d.pnm'%j,'w'))
#    map2img(vram,0x1C00,False,cpal).write(open('map_9C00_neg_%02d.pnm'%j,'w'))
#    cpal2img(cpal).write(open('pal_%02d.pnm'%j,'w'))
t.dump_insts()
##################################

GBC.close()
