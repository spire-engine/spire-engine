rm -f rendercommands.txt
$1 -enginedir ./EngineContent -dir ./ExampleGame -no_renderer -headless -runforframes 3
if [ $(grep 'Present' rendercommands.txt | wc -l) = 3 ] ; then
	echo "passed test 'Integration Test'" 
else
  echo "failed test 'Integration Test'" 
fi
rm -f rendercommands.txt