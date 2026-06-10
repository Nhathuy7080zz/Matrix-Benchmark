@echo off
if exist results.csv del results.csv

echo Compiling . . .
gcc -O3 Single.c -o Single.exe
gcc -O3 Multi.c -o Multi.exe
gcc -O3 GPU.c -o GPU.exe -I.\OpenCL-Headers -DCL_TARGET_OPENCL_VERSION=120 C:\Windows\System32\OpenCL.dll

cls
.\Single.exe
.\Multi.exe
.\GPU.exe

echo.
echo Processing results...
:: Tinh toan Speedup va Efficiency - Cap nhat dung vong lap foreach de khong bi drop du lieu
echo [System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture > process.ps1
echo $c = Import-Csv 'results.csv' >> process.ps1
echo $b = [double]($c[0].GFLOPS -replace '[^^\d.]', '') >> process.ps1
echo foreach ($row in $c) { >> process.ps1
echo     $g = [double]($row.GFLOPS -replace '[^^\d.]', '') >> process.ps1
echo     $s = $g / $b >> process.ps1
echo     $e = 'N/A' >> process.ps1
echo     if ($row.Mode -eq 'Single') { $e = '100.00' + [char]37 } >> process.ps1
echo     elseif ($row.Mode -eq 'Multi') { $e = ('{0:N2}' -f (($s / [double]$row.Threads) * 100)) + [char]37 } >> process.ps1
echo     $row ^| Add-Member -MemberType NoteProperty -Name 'Speedup' -Value ('{0:N2}x' -f $s) >> process.ps1
echo     $row ^| Add-Member -MemberType NoteProperty -Name 'Efficiency' -Value $e >> process.ps1
echo } >> process.ps1
echo $c ^| Export-Csv 'results.csv' -NoTypeInformation >> process.ps1

powershell -NoProfile -ExecutionPolicy Bypass -File process.ps1
del process.ps1

pause