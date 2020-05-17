./x64/Release/GameEngine -enginedir EngineContent -dir ExampleGame -no_renderer -headless -runforframes 3 | Out-Null
$Matches = Select-String -Path "rendercommands.txt" -Pattern "Present" -AllMatches
if ($Matches.Matches.Count -eq 3)
{
  Remove-Item rendercommands.txt
  "passed test 'Integration Test'"
  "<assemblies><aseembly name='GameEngine' total=1 passed=1><collection name='Smoke' total=1 passed=1><test name='Smoke' result='Pass'/></collection></assembly></assemblies>" >> test_result.xml
}
else
{
  "failed test 'Integration Test'"
  "<assemblies><aseembly name='GameEngine' total=1 failed=1><collection name='Smoke' total=1 failed=1><test name='Smoke' result='Fail'/></collection></assembly></assemblies>" >> test_result.xml
}
$wc = New-Object 'System.Net.WebClient'
$wc.UploadFile("https://ci.appveyor.com/api/testresults/xunit/$($env:APPVEYOR_JOB_ID)", (Resolve-Path .\test_result.xml))