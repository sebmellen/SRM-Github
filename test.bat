rem test srm.exe

echo "the remove test" > test.txt
%1\srm.exe -v test.txt

echo "the remove test" > test.txt
echo "first alternate" > test.txt:first
echo "and the 2nd alternate data stream" > test.txt:second
%1\srm.exe -vvv test.txt
IF EXIST test.txt (
  echo could not remove test.txt in ads test!
  exit
)

echo "The hard link test" > test2.txt
del link.txt
%1\ln.exe link.txt test2.txt
%1\srm.exe -vvv test2.txt
%1\srm.exe -vvv link.txt
IF EXIST test2.txt (
  echo could not remove test2.txt in hard link test!
  exit
)
IF EXIST link.txt (
  echo could not remove link.txt in hard link test!
  exit
)

echo "The wildcard test" > test1.txt
echo "Another test file" > test2.txt
echo "and a 4rd" > test3.txt
%1\srm.exe -vvv test*.txt
echo ""
IF EXIST test1.txt (
  echo could not remove test1.txt in wildcard test!
  exit
)
IF EXIST test2.txt (
  echo could not remove test2.txt in wildcard test!
  exit
)
IF EXIST test3.txt (
  echo could not remove test3.txt in wildcard test!
  exit
)

rem don't run test with large file by default
IF "a" == "b" (
  echo "Test a 2.1GB file"
  %1\createfile.exe big.txt 2100
  %1\srm.exe -svvv big.txt
  IF EXIST big.txt (
    echo "could not remove big.txt"
    exit
  )
)

echo ""
echo *** all Windows tests successful. ***
