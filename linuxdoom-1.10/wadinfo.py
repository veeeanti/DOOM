import struct, os  
wad='doom.wad'  
if not os.path.exists(wad):  
    raise SystemExit('doom.wad not found')  
with open(wad,'rb') as f:  
    d=f.read()  
