#include <string>
#include <set>

#include <cassert>
#include <unistd.h>

#include <srm.h>
#include <impl.h>

/**
 * @return true if all files/directories could be removed; false otherwise.
 */
static int delFn(const std::string& fn, const int options)
{
  int flag=FTS_F, ret = 1;

  struct stat statbuf;
  if(lstat(fn.c_str(), &statbuf) == 0)
  {
    if(S_ISDIR(statbuf.st_mode))
    {
      std::string spec=fn; spec+="\\*.*";

      struct _finddata_t fileinfo;
      intptr_t h=_findfirst(spec.c_str(), &fileinfo);
      if(h == -1) return 0;

      do
      {
	std::string fi=fileinfo.name;
        if(fi!="." && fi!="..") {
          if (! delFn(fn+'\\'+fi, options)) {
	    ret = 0;
	  }
	}
      } while(_findnext(h, &fileinfo) == 0);
      _findclose(h);

      flag=FTS_DP;
    }
  }

  if (! process_file(const_cast<char*>(fn.c_str()), flag, options)) {
    ret = 0;
  }
  return ret;
}

/**
 * @return 0 if all files/directories could be removed; > 0 otherwise.
 */
extern "C" int tree_walker(char **trees, const int options)
{
  int i = 0, ret = 0;
  if(!trees) return +2;

  while (trees[i] != NULL)
  { 
    while (trees[i][strlen(trees[i]) - 1] == SRM_DIRSEP)
      trees[i][strlen(trees[i]) -1] = '\0';

    if (! delFn(trees[i++], options)) {
      ret = +1;
    }
  }
  return ret;
}
