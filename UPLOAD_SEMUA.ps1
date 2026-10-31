# Satu klik: kirim seluruh proyek ke GitHub, tunggu build APK, unduh APK ke folder ini.
$repoUrl  = 'https://github.com/PTRFarhan/esp32-tx-app.git'
$repoSlug = 'PTRFarhan/esp32-tx-app'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Set-Location $PSScriptRoot

function Fail([string]$m) {
  Write-Host ""
  Write-Host $m -ForegroundColor Red
  Read-Host 'Tekan Enter untuk menutup'
  exit 1
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
  Start-Process 'https://git-scm.com/download/win'
  Fail 'Git belum terpasang. Pasang dari halaman yang terbuka (Next terus), lalu klik UPLOAD_SEMUA.bat lagi.'
}

if (-not (Test-Path '.git')) { git init | Out-Null }
git add -A
$stamp = Get-Date -Format 'dd/MM/yyyy HH.mm.ss'
git -c user.name='ESP32TX' -c user.email='esp32tx@users.noreply.github.com' commit -m "Update $stamp" | Out-Null
git branch -M main
git remote remove origin 2>$null
git remote add origin $repoUrl

Write-Host '1/4 Mengirim seluruh proyek ke GitHub (kalau muncul jendela login, masuk dan setujui)...' -ForegroundColor Cyan
git push -u origin main --force
if ($LASTEXITCODE -ne 0) { Fail 'Push gagal. Cek koneksi internet dan login GitHub, lalu klik UPLOAD_SEMUA.bat lagi.' }
$sha = (git rev-parse HEAD).Trim()
Write-Host "Terkirim. Lihat di: https://github.com/$repoSlug" -ForegroundColor Green

Write-Host '2/4 Menunggu GitHub membangun APK (sekitar 3 sampai 5 menit)...' -ForegroundColor Cyan
$api = "https://api.github.com/repos/$repoSlug/actions/runs?head_sha=$sha"
$headers = @{ 'User-Agent' = 'esp32tx-builder' }
$deadline = (Get-Date).AddMinutes(20)
$run = $null
while ((Get-Date) -lt $deadline) {
  $r = $null
  try { $r = Invoke-RestMethod -Uri $api -Headers $headers } catch { }
  if ($r -and $r.total_count -gt 0) {
    $run = $r.workflow_runs[0]
    Write-Host ("   status build: " + $run.status)
    if ($run.status -eq 'completed') { break }
  } else {
    Write-Host '   menunggu GitHub memulai build...'
  }
  Start-Sleep -Seconds 10
}
if (-not $run -or $run.status -ne 'completed') { Start-Process "https://github.com/$repoSlug/actions"; Fail 'Build belum selesai dalam 20 menit. Halaman Actions dibuka, cek di sana. Proyek sudah terkirim ke GitHub.' }
if ($run.conclusion -ne 'success') { Start-Process $run.html_url; Fail 'Build APK GAGAL (proyek tetap terkirim ke GitHub). Halaman build dibuka. Salin pesan error merahnya dan kirim ke Claude.' }

Write-Host '3/4 Mengunduh APK...' -ForegroundColor Cyan
$apkUrl = "https://github.com/$repoSlug/releases/download/latest/ESP32-TX.apk"
New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot 'apk') | Out-Null
$out = Join-Path $PSScriptRoot 'apk\ESP32-TX.apk'
$ok = $false
for ($i = 0; $i -lt 6 -and -not $ok; $i++) {
  try { Invoke-WebRequest -Uri $apkUrl -OutFile $out -UseBasicParsing; $ok = $true } catch { Start-Sleep -Seconds 5 }
}
if (-not $ok) { Start-Process "https://github.com/$repoSlug/releases/tag/latest"; Fail 'Unduh otomatis gagal. Halaman Releases dibuka, unduh ESP32-TX.apk dari sana.' }

Write-Host '4/4 Menaruh file .apk di dalam repository GitHub (folder apk/)...' -ForegroundColor Cyan
git add -f apk/ESP32-TX.apk
git -c user.name='ESP32TX' -c user.email='esp32tx@users.noreply.github.com' commit -m "Tambah APK terbaru [skip ci]" | Out-Null
git push origin main
if ($LASTEXITCODE -ne 0) { Fail 'APK terunduh di folder apk\, tetapi gagal dikirim ke repo. Klik UPLOAD_SEMUA.bat lagi.' }

Write-Host ""
Write-Host "SELESAI. APK ada di: $out" -ForegroundColor Green
Write-Host "File APK di GitHub: https://github.com/$repoSlug/raw/main/apk/ESP32-TX.apk"
Write-Host "Proyek lengkap: https://github.com/$repoSlug"
Start-Process explorer.exe "/select,`"$out`""
Read-Host 'Tekan Enter untuk menutup'
