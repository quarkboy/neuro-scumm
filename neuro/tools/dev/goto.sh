# Shell functions for use with t.sh: source this file (". goto.sh") from the folder it is in.
#   goto NAME [SECONDS] [WIDTH]   walk to something and wait for the next room
#   choose N                      pick a line and wait for the next choice
S=${S:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}
goto () 
{ 
    n=$(wc -l < $S/game.out);
    $S/t.sh act interact "{\"verb\":\"Walk to\",\"target\":\"$1\"}";
    for i in $(seq 1 ${2:-40});
    do
        tail -n +$n $S/game.out | grep -aq "Location:" && break;
        sleep 1;
    done;
    sleep 1;
    tail -n +$n $S/game.out | grep -a '"context"\|result' | sed 's/.*message":"//' | cut -c1-${3:-200}
}
choose () 
{ 
    n=$(grep -ac "actions./force" $S/game.out);
    $S/t.sh act choose_dialogue "{\"option\":$1}";
    for i in $(seq 1 90);
    do
        [ $(grep -ac "actions./force" $S/game.out) -gt $n ] && break;
        sleep 1;
    done
}
