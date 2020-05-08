$personalAccessToken = "a24eb1b3f90011ccd912a42f82f4f865df9ff0f6"
$assetDirName = "ExampleGame"
$fileName = "ExampleGame.zip"
$repoName = "csyonghe/SpireEngine"
$fileIndex = 0
Add-Type -assembly "system.io.compression.filesystem"

if (-NOT ((Test-Path $assetDirName))) {

"ExampleGame directory not found, downloading..."
# First, query asset ID from latest release.
$queryUri = "https://api.github.com/repos/$repoName/releases"
$queryRs = Invoke-WebRequest -Method "GET"  -Headers @{Authorization = "token $personalAccessToken" } -Uri $queryUri

# Parse returned json response to get asset ID.
$queryJson = ConvertFrom-Json -InputObject $queryRs
$assetId = $queryJson[0].assets[$fileIndex].id

# Download asset and save it to file
Invoke-WebRequest -Method "GET"  -Headers @{Authorization = "token $personalAccessToken"; Accept = "application/octet-stream"} -Uri "https://api.github.com/repos/$repoName/releases/assets/$assetId" -OutFile $fileName

# Unzip.
[System.IO.Compression.ZipFile]::ExtractToDirectory($fileName, '.\')

# Delete downloaded zip file.
Remove-Item $fileName

"$assetDirName downloaded and updated."
}
else {
"Directory $assetDirName already exists, aborted."
}
cmd /c pause | out-null