# Repo owner (user id)
OWNER="spire-engine"
# Repo name
REPO="engine"
#The file name expected to download. This is deleted before curl pulls down a new one
FILE_NAME="ExampleGame.zip"


# Concatenate the values together for a 
API_URL="https://api.github.com/repos/$OWNER/$REPO"
if ! [ -x "$(command -v jq)" ]; then
if [[ $EUID -ne 0 ]]; then
  sudo apt-get install jq
else
  echo "Please install jq first:"
  echo " sudo apt-get install jq"
  exit 0
fi
fi
if ! [ -x "$(command -v curl)" ]; then
if [[ $EUID -ne 0 ]]; then
  sudo apt-get install jq
else
  echo "Please install curl first:"
  echo " sudo apt-get install curl"
  exit 0
fi
fi
if ! [ -x "$(command -v unzip)" ]; then
if [[ $EUID -ne 0 ]]; then
  sudo apt-get install unzip
else
  echo "Please install unzip first:"
  echo " sudo apt-get install unzip"
  exit 0
fi
fi
# Gets info on latest release, gets first uploaded asset id of a release,
# More info on jq being used to parse json: https://stedolan.github.io/jq/tutorial/
ASSET_ID=$(curl $API_URL/releases/latest | jq -r '.assets[0].id')
echo "Asset ID: $ASSET_ID"
# curl does not allow overwriting file from -O, nuke
rm -f $FILE_NAME
# curl:
# -O: Use name provided from endpoint
# -J: "Content Disposition" header, in this case "attachment"
# -L: Follow links, we actually get forwarded in this request
# -H "Accept: application/octet-stream": Tells api we want to dl the full binary
curl -O -J -L -H "Accept: application/octet-stream" "$API_URL/releases/assets/$ASSET_ID"

unzip $FILE_NAME
rm -f $FILE_NAME
