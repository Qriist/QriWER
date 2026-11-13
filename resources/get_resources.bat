rmdir /s /q jiwer
mkdir jiwer
cd jiwer
gh release download --repo jitsi/jiwer --archive zip
for %%f in (*.zip) do (
    mkdir "%%~nf"
    tar -xf "%%f" -C "%%~nf"
)
DEL *.zip
cd ..

rmdir /s /q rapidfuzz
mkdir rapidfuzz
cd rapidfuzz
gh release download --repo rapidfuzz/RapidFuzz --archive zip
for %%f in (*.zip) do (
    mkdir "%%~nf"
    tar -xf "%%f" -C "%%~nf"
)
DEL *.zip
cd ..

rmdir /s /q rapidfuzz-cpp
git clone https://github.com/rapidfuzz/rapidfuzz-cpp.git rapidfuzz-cpp

rmdir /s /q taskflow
git clone https://github.com/taskflow/taskflow.git taskflow