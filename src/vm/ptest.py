import os
import sys

c1 = "make tests/vm/page-parallel.result"
c2 = "rm -f tests/vm/page-parallel.output"

dir ="build" 

os.chdir(dir)

for i in range(0, int(sys.argv[1])):
  os.system(c1)
  os.system(c2)
