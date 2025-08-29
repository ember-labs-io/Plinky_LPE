cd sw/nocube_makefile
make clean
make -j BUILD_TYPE=RELEASE
python3 nocube_binmaker.py
cd ../..

