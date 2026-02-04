# mfg

maanoo's find and grep

Search for files in a directory hierarchy optionally matching a pattern and optionally print their content that match a pattern.

Implemented with `liburing`, `memmem` and `fts`.

![Screenshot](TODO)

## Usage

### Options

```
       mfg [-bqpmta] FILE-TYPE [-ni] [NAME-PATTERN] [-nioma] [CONTENT-PATTERN]

DESCRIPTION
       mfg Search for files in a directory hierarchy optionally matching a pattern and optionally print their content that match a pattern.

       FILE-TYPE: a(all), f(files), d(directories), e(executables), t(textfiles), b(binary)

       NAME-PATTERN:
       - .
       - TEXT
       - [prefix|start|extension|end|fullname]: TEXT[,TEXT]

       CONTENT-PATTERN:
       - .
       - TEXT
       - [begin|start|end|]: TEXT
       - wrapped: START-TEXT END-TEXT
       - regex: <regex>

OPTIONS
   General options
       -b     BFS search
       -q     Output only the names of the files that the content pattern has matched
       -p     No color and no truncated output
       -m     Monochrome output
       -t     Table output
       -a     Don't search hidden files and directories

   Name options
       -n     Do not output the file names
       -i     Case sensitive file name pattern matching

   Content options
       -n     Do not output the matched content lines
       -i     Case sensitive content pattern matching
       -o     Output only the matched content
       -m     Multiline content pattern matching
       -a     Output also lines around the matched content lines

EXIT STATUS
       0      Successful program execution.
       1      Usage, syntax or configuration file error.
       2      Operational error.
```

## Install

```
git clone https://github.com/MaanooAk/mfg
cd mfg
make
sudo make install
```

Arch Linux: [AUR package](https://aur.archlinux.org/packages/mfg/).
