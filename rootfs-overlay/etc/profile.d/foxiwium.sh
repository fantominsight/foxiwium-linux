# Foxiwium — interactive shell branding
if [ -n "$PS1" ]; then
    export EDITOR=vim
    export VISUAL=vim
    export PS1='\[\033[1;33m\]\u@foxiwium\[\033[0m\]:\w\$ '
fi
