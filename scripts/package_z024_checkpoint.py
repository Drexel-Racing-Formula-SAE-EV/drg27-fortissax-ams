#!/usr/bin/env python3
"""Package source/evidence, excluding generated binaries; verify extracted hashes."""
from pathlib import Path
import hashlib,json,sys,tempfile,zipfile
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.parent.mkdir(parents=True,exist_ok=True)
entries={}
for p in sorted(root.rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(root)
 if any(x in {'.git','__pycache__','build','build-null','build_null'} or x.startswith('build-') for x in rel.parts):continue
 if p.suffix in {'.pyc','.o','.a','.so','.plist'} or p.name in {'Z022_WORKTREE_SHA256SUMS.txt','Z023_WORKTREE_SHA256SUMS.txt','Z024_WORKTREE_SHA256SUMS.txt','core'}:continue
 data=p.read_bytes()
 if data.startswith(b'\x7fELF'):continue
 entries[str(rel)]=data
manifest=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in entries.items())
entries['Z024_WORKTREE_SHA256SUMS.txt']=manifest.encode()
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
 for name,data in entries.items():z.writestr('DER27-AMS-zephyr/'+name,data)
with tempfile.TemporaryDirectory(prefix='z024-verify-') as td:
 with zipfile.ZipFile(out) as z:
  assert z.testzip() is None
  z.extractall(td)
 base=Path(td)/'DER27-AMS-zephyr'
 for line in manifest.splitlines():
  digest,name=line.split('  ',1)
  assert hashlib.sha256((base/name).read_bytes()).hexdigest()==digest,name
 assert len([p for p in base.rglob('*') if p.is_file()])==len(entries)
print(json.dumps({'archive':str(out),'files':len(entries),'sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'verified':True}))
