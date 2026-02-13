rm compute_manual
cd build
# rm -rf *
cmake ..
make compute_manual
mv compute_manual ../
cd ..