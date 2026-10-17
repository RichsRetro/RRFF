import hashlib,struct,sys
from pathlib import Path
p=Path(sys.argv[1]);data=bytearray(p.read_bytes());code=bytes(data[4096:])
expected="33ceb2d22c2f6c81b48f61bcd98615eee4741415f84e436b5ac274411045c33d"
if len(data)!=9432 or hashlib.sha256(code).hexdigest()!=expected:raise SystemExit("Code/data does not match the distributed patcher; check NASM version and configuration")
data[0x378:0x38c]=hashlib.sha1(struct.pack("<I",len(code))+code).digest();p.write_bytes(data)
print("Verified matching code/data and updated section digest; XBE security signature is unsigned")
