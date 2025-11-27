#!/bin/bash                                                                                                                                  

DATAFOLDER="/nfs/NASPG/BTData/Jun2021_FOOT_data"

#-------------------------------------------------------------------------------------                                                       
                                                                                                                                             
    ls -lrt $DATAFOLDER/rawdata/ | grep data | awk '{ print $9}' > FILELIST.txt                                                            
                                                                                                                                             
    if [ ! -f FILELIST.old.txt ]; then                                                                                                       
        touch FILELIST.old.txt                                                                                                               
    fi                                                                                                                                       
                                                                                                                                             
    for i in `diff -y -W 140 --suppress-common-lines FILELIST.txt FILELIST.old.txt | awk '{ print $1 }'`;                                           
    do                                                                                                                                       
    COMMAND="../FOOT_compress $DATAFOLDER/rawdata/$i $DATAFOLDER/compressed/FOOT_$(echo $i | cut -d'.' -f 2 | sed 's/^0*//')_$(echo $i | cut -d'_' -f 6 | sed 's/^0*//').root" 
        echo $COMMAND                                                                                                                        
        $COMMAND                                                                                                                             
    done                                                                                                                                     
                                                                                                                                             
    cp FILELIST.txt FILELIST.old.txt                                                                                                         
                                                                                                                                             
#-------------------------------------------------------------------------------------     

