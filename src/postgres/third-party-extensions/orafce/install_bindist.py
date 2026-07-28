import os
import sys
import shutil

os.makedirs(sys.argv[2], exist_ok=True)
shutil.copy(sys.argv[3], os.path.join(sys.argv[2], sys.argv[4]))

for f in sys.argv[5:]:
	shutil.copy(os.path.join(sys.argv[1], f), os.path.join(sys.argv[2], f))