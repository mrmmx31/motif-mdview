import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
exe=root/'build/mdv-core'
def run(p):
 return subprocess.run([str(exe),'--text',str(p)],capture_output=True,timeout=5)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'texto com espaços; $(não executa).md'
 p.write_text('# Olá\n\n**Teste**\n',encoding='utf-8')
 before=hashlib.sha256(p.read_bytes()).hexdigest()
 r=run(p)
 assert r.returncode==0 and 'Olá'.encode() in r.stdout
 assert hashlib.sha256(p.read_bytes()).hexdigest()==before
 p.chmod(0o444)
 assert run(p).returncode==0
 assert run(Path(tmp)/'ausente.md').returncode!=0
 assert run(tmp).returncode!=0
 fifo=Path(tmp)/'pipe.md';os.mkfifo(fifo)
 assert run(fifo).returncode!=0
 p.chmod(0o644);p.write_bytes(b'x\0y');assert run(p).returncode!=0
 p.write_bytes(b'\x1b]0;nao-injetar\x07');r=run(p)
 assert r.returncode==0 and b'\x1b' not in r.stdout and b'\x07' not in r.stdout
 p.write_bytes(b'x'*(8*1024*1024+1));assert run(p).returncode!=0
print('PASS: 9 verificações de arquivo, permissões, limites e ausência de escrita.')
