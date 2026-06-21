# Publishing this source tree to GitHub

This folder is already separated as a source-only copy:

`C:\Users\Maggie\Documents\Browsers\UXP-kernelex-github-source`

It excludes the local Git metadata, object directories, build logs, local
`.mozconfig`, and packaged browser binaries.

## First upload

1. Create a new empty repository on GitHub.
   Do not add a README, license, or `.gitignore` from the GitHub web UI, because
   this source tree already has those files.

2. Open PowerShell or Git Bash in this folder:

   ```bat
   cd /d C:\Users\Maggie\Documents\Browsers\UXP-kernelex-github-source
   ```

3. Initialize Git:

   ```bat
   git init
   git branch -M main
   ```

4. Review what will be committed:

   ```bat
   git status --short
   ```

5. Commit the source:

   ```bat
   git add .
   git commit -m "Initial Windows 98 KernelEx UXP source import"
   ```

6. Connect your GitHub repository:

   ```bat
   git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
   ```

7. Push it:

   ```bat
   git push -u origin main
   ```

## Publishing builds

Do not commit compiled browser folders, object directories, or ZIP files to the
source repository.

For binaries:

1. Go to the repository on GitHub.
2. Open Releases.
3. Create a new release.
4. Upload the ZIP made from the separate `palemoon` runtime folder.

## Future source updates

After making code changes:

```bat
cd /d C:\Users\Maggie\Documents\Browsers\UXP-kernelex-github-source
git status
git add .
git commit -m "Describe the fix"
git push
```

Keep machine-specific files out of commits:

- `.mozconfig`
- `obj-*`
- `buildlog.txt`
- packaged browser folders
- crash dumps and debugger logs
