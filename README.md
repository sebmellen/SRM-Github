# SRM-Github
A port of SRM from Sourceforge to Github. Good code that deserves to be shared.

### SRM - secure file deletion for posix systems

- srm is a secure replacement for rm(1). Unlike the standard rm, it overwrites the data in the target files before unlinking them. This prevents command-line recovery of the data by examining the raw block device. It may also help frustrate physical examination of the disk, although it's unlikely that it can completely prevent that type of recovery. It is, essentially, a paper shredder for sensitive files.
- srm is ideal for personal computers or workstations with Internet connections. It can help prevent malicious users from breaking in and undeleting personal files, such as old emails. Because it uses the exact same options as rm(1), srm is simple to use. Just subsitute it for rm whenever you want to destroy files, rather than just unlinking them. For more information on using srm, read the manual page srm(1).
