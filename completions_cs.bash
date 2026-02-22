_cs_complete() {
  local cur prev
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"

  local opts="--cc --cflags --ldflags --cache-dir --no-cache --verbose -v --version -u --update --help"

  case "$prev" in
    --cache-dir)
      COMPREPLY=( $(compgen -d -- "$cur") )
      return 0
      ;;
    --cc)
      COMPREPLY=( $(compgen -W "cc gcc clang" -- "$cur") )
      return 0
      ;;
    --cflags|--ldflags)
      COMPREPLY=()
      return 0
      ;;
  esac

  if [[ "$cur" == -* ]]; then
    COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
    return 0
  fi

  COMPREPLY=( $(compgen -f -X '!*.c' -- "$cur") )
  return 0
}

complete -o filenames -F _cs_complete cs
