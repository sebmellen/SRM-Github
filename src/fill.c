/* this file is part of srm http://srm.sourceforge.net/
   It is licensed under the MIT/X11 license */

void fill(unsigned char *dst, unsigned dst_len, const unsigned char *src, const unsigned src_len)
{
  unsigned i = 0;
  while(dst_len > 0) {
    *dst++ = src[i];
    if (++i == src_len) {
      i = 0;
    }
    --dst_len;
  }
}
