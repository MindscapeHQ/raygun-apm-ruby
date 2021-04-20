#!/bin/bash
gemVersion=$1
apiKey=$2
deployUrl=$3

if [ $# -eq 4 ]; then
    platform="-${4}"
else
    platform=""
fi

echo "Zipping the pkg folder to send ot Octopus..."
zipFileName="raygun-apm${platform}.${gemVersion}.zip"
gemFileFolder="pkg"
rm -f $zipFileName
zip -r $zipFileName $gemFileFolder
echo "Sending request to octopus API..."
curl -X POST $deployUrl -H "X-Octopus-ApiKey: ${apiKey}" -F "data=@${zipFileName}"