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