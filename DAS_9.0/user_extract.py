import re
s=open('history_messages.txt',encoding='utf-8').read().splitlines()
inuser=False
for i,line in enumerate(s):
 if line.startswith('===== LINE'):
  inuser=' user ' in line
  if inuser: print('\n'+line)
 elif inuser and line.strip():
  print(line[:1200])
