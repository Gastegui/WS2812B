#!/bin/bash

exec 3>/dev/tcp/192.168.1.220/3333

printf "\x00\x00" >&3

# Run cava in raw mode and read its output
cava -p ./cava_config | while IFS= read -r line; do
    # Here $line contains one line of cava's raw ASCII output
    #echo "Got line: $line"
    IFS=';' read -ra numbers <<< "$line"
    i=264
    for n in "${numbers[@]}"; do
        printf "\\x%02x" "$(( i >> 8 ))" >&3 #Send pixel number
        printf "\\x%02x" "$(( i & 0xFF ))" >&3 #Send pixel number
        printf "\\x%02x" "127" >&3 #Send Hue
        printf "\\xFF" >&3 #Send saturation
        printf "\\x%02x" "$n" >&3 #Send Value
        ((i--))
    done
    echo ""
done

exec 3>&-
