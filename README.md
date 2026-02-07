# mfg

maanoo's find and grep

Search for files in a directory hierarchy optionally matching a pattern and optionally print their content that match a pattern.

Supports pattern type selection, bfs file search, echo filename if greps, multicolor matches, handles matches in very long lines, builtin directory excludes.

Implemented with `liburing` and `memmem`.

![Screenshot](https://i.imgur.com/hz7zJvy.png)

## Usage

### Options

```
       mfg [-bqpmtav] FILE-TYPE [-ni] [NAME-PATTERN] [-nioma] [CONTENT-PATTERN]
       mfg [-bqpmtav] FILE-TYPE [-ni] [NAME-PATTERN] [-nioma] [CONTENT-PATTERN] -- ROOT[,ROOT]

DESCRIPTION
       mfg Search for files in a directory hierarchy optionally matching a pattern and
       optionally print their content that match a pattern.

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
       -q     Query, output only the names of the files that the content pattern has matched
       -p     Plain, no color and no truncated output
       -m     Monochrome output
       -t     Table output
       -a     All no, don't search hidden files and directories
       -v     Verbose, print all errors

   Name options
       -c     Case sensitive file name pattern matching
       -n     Do not output the file names

   Content options
       -c     Case sensitive content pattern matching
       -n     Do not output the matched content lines
       -o     Output only the matched content
       -m     Multiline content pattern matching
       -a     Around, output also lines around the matched content lines

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
