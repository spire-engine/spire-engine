rm -f rendercommands.txt
$1 -enginedir ./EngineContent -dir ./ExampleGame -no_renderer -headless -runforframes 3
if [ $(grep 'Present' rendercommands.txt | wc -l) = 4 ] ; then
  rm -f rendercommands.txt
  echo "passed test 'Integration Test'"
  exit 0
else
  rm -f rendercommands.txt
  echo "failed test 'Integration Test'"
  exit 1
fi