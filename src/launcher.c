/*
*      launcher.c
*      Copyright © 2008, 2009 Martin Duquesnoy <xorg62@gmail.com>
*      All rights reserved.
*
*      Redistribution and use in source and binary forms, with or without
*      modification, are permitted provided that the following conditions are
*      met:
*
*      * Redistributions of source code must retain the above copyright
*        notice, this list of conditions and the following disclaimer.
*      * Redistributions in binary form must reproduce the above
*        copyright notice, this list of conditions and the following disclaimer
*        in the documentation and/or other materials provided with the
*        distribution.
*      * Neither the name of the  nor the names of its
*        contributors may be used to endorse or promote products derived from
*        this software without specific prior written permission.
*
*      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
*      A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
*      OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
*      SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
*      LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*      DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
*      THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
*      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
*      OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "wmfs.h"

struct Complete_cache {
     char *start;
     char **namelist;
     size_t hits;
};

static int
fts_alphasort(const FTSENT **a, const FTSENT **b)
{
     return (strcmp((*a)->fts_name, (*b)->fts_name));
}

/*
 * Just search command in PATH.
 * Return the characters to complete the command.
 */
static char **
complete_on_command(char *start)
{
     char **paths, *path, *p, **namelist = NULL;
     int count;
     FTS *tree;
     FTSENT *node;

     if (!(path = getenv("PATH")) || !start)
          return NULL;

     /* split PATH into paths */
     path = p = xstrdup(path);

     for (count = 1, p = path; strchr(p, ':') != NULL; p++, count++);

     paths = xcalloc(count, sizeof(*paths));

     for (paths[0] = p = path, count = 1; (p = strchr(p, ':')) != NULL; p++, count++) {
          paths[count] = p+1;
          *p = '\0';
     }
     paths[count] = NULL;

     if ((tree = fts_open(paths, FTS_NOCHDIR, fts_alphasort)) == NULL) {
          warn("fts_open");
          free(paths);
          free(path);
          return NULL;
     }

     count = 0;
     while ((node = fts_read(tree)) != NULL) {

          if (node->fts_level > 0)
               fts_set(tree, node, FTS_SKIP);

          if (node->fts_level != 0 &&
                    node->fts_info & FTS_F &&
                    node->fts_info & FTS_NS &&
                    (node->fts_statp->st_mode & S_IXOTH) != 0 &&
                    strncmp(node->fts_name, start, strlen(start)) == 0) {
               namelist = xrealloc(namelist, ++count, sizeof(*namelist));
               namelist[count-1] = xstrdup(node->fts_name + strlen(start));
          }
     }

     if (count) {
          namelist = xrealloc(namelist, ++count, sizeof(*namelist));
          namelist[count-1] = NULL;
     }

     if (fts_close(tree))
          warn("fts_close");

     free(paths);
     free(path);

     return namelist;
}

/*
 * Complete a filename or directory name.
 * works like complete_on_command.
 */
static char **
complete_on_files(char *start)
{
     char *p, *home, *path, *dirname = NULL, *paths[2], **namelist = NULL;
     int count;
     FTS *tree;
     FTSENT *node;

     p = start;

     /*
      * Search the directory to open and set
      * the beginning of file to complete on pointer 'p'.
      */
     if (*(p) == '\0' || !strrchr(p, '/'))
          path = xstrdup(".");
     else
     {

          if (!(home = getenv("HOME")))
               return NULL;

          /* remplace ~ by $HOME in dirname */
          if (!strncmp(p, "~/", 2) && home)
               xasprintf(&dirname, "%s%s", home, p+1);
          else
               dirname = xstrdup(p);

          /* Set p to filename to be complete
           * and path the directory containing the file
           * /foooooo/baaaaaar/somethinglikethis<tab>
           * <---- path - ---><------- p ------>
           */
          p = strrchr(dirname, '/');
          if (p != dirname)
          {
               *(p++) = '\0';
               path = xstrdup(dirname);
          }
          else
          {
               path = xstrdup("/");
               p++;
          }
     }

     paths[0] = path;
     paths[1] = NULL;

     if ((tree = fts_open(paths, FTS_NOCHDIR, fts_alphasort)) == NULL)
     {
          warn("fts_open");
          free(dirname);
          free(path);
          return NULL;
     }

     count = 0;
     while ((node = fts_read(tree)) != NULL) {
          if (node->fts_level > 0)
               fts_set(tree, node, FTS_SKIP);

          if (node->fts_level != 0 &&
                    strncmp(node->fts_name, p, strlen(p)) == 0) {
               namelist = xrealloc(namelist, ++count, sizeof(*namelist));
               namelist[count-1] = xstrdup(node->fts_name + strlen(p));
          }
     }

     if (count) {
          namelist = xrealloc(namelist, ++count, sizeof(*namelist));
          namelist[count-1] = NULL;
     }

     if (fts_close(tree))
          warn("fts_close");

     free(dirname);
     free(path);

     return namelist;
}

static void
complete_cache_free(struct Complete_cache *cache)
{
     int i;

     /* release memory */
     free(cache->start);

     if (cache->namelist) {
          for (i = 0; cache->namelist[i]; i++)
               free(cache->namelist[i]);
          free(cache->namelist);
     }

     /* init */
     cache->hits = 0;
     cache->start = NULL;
     cache->namelist = NULL;
}

static char *
complete(struct Complete_cache *cache, char *start)
{
     char *p = NULL, *comp = NULL;

     if (!start || !cache)
          return NULL;

     if ((p = strrchr(start, ' ')))
          p++;
     else
          p = start;

     if (cache->start && strcmp(cache->start, start) == 0) {
          if (cache->namelist && !cache->namelist[cache->hits])
               cache->hits = 0;
     }
     else {

          complete_cache_free(cache);
          cache->start = xstrdup(start);

          if (p == start)
               cache->namelist = complete_on_command(p);
          else
               cache->namelist = complete_on_files(p);
     }

     if (cache->namelist && cache->namelist[cache->hits])
          comp = cache->namelist[cache->hits];

     return comp;
}

static void
launcher_execute(Launcher *launcher)
{
     BarWindow *bw;
     bool found;
     bool lastwastab = False;
     bool my_guitar_gently_wheeps = True;
     char tmp[32] = { 0 };
     char buf[512] = { 0 };
     char tmpbuf[512] = { 0 };
     char *end;
     int i, pos = 0, histpos = 0, x, w;
     KeySym ks;
     XEvent ev;
     struct Complete_cache cache = {NULL, NULL, 0};

     screen_get_sel();

     x = (conf.layout_placement)
          ? (infobar[selscreen].tags_board->geo.x + infobar[selscreen].tags_board->geo.width)
          : (infobar[selscreen].layout_button->geo.x + infobar[selscreen].layout_button->geo.width);

     XGrabKeyboard(dpy, ROOT, True, GrabModeAsync, GrabModeAsync, CurrentTime);

     w = (launcher->width ? launcher->width : infobar[selscreen].bar->geo.width - x - 1);

     bw = barwin_create(infobar[selscreen].bar->win, x, 1, w,
                        /* infobar[selscreen].bar->geo.width - x - 1, */
                        infobar[selscreen].bar->geo.height - 2,
                        infobar[selscreen].bar->bg,
                        infobar[selscreen].bar->fg,
                        False, False, conf.border.bar);

     barwin_map(bw);
     barwin_refresh_color(bw);

     /* First draw of the cursor */
     XSetForeground(dpy, gc, getcolor(infobar[selscreen].bar->fg));

     XDrawLine(dpy, bw->dr, gc,
               1 + textw(launcher->prompt) + textw(" ") + textw(buf), 2,
               1 + textw(launcher->prompt) + textw(" ") + textw(buf), INFOBARH - 4);

     barwin_refresh(bw);

     barwin_draw_text(bw, 1, FHINFOBAR - 1, launcher->prompt);

     while(my_guitar_gently_wheeps)
     {
          if(ev.type == KeyPress)
          {
               XLookupString(&ev.xkey, tmp, sizeof(tmp), &ks, 0);

               /* Check Ctrl-c / Ctrl-d */
               if(ev.xkey.state & ControlMask)
               {
                    if(ks == XK_c || ks == XK_d)
                         ks = XK_Escape;
                    else if(ks == XK_p)
                         ks = XK_Up;
                    else if(ks == XK_n)
                         ks = XK_Down;
               }

               /* Check if there is a keypad */
               if(IsKeypadKey(ks) && ks == XK_KP_Enter)
                    ks = XK_Return;

               switch(ks)
               {
               case XK_Up:
                    if(launcher->nhisto)
                    {
                         if(histpos >= (int)launcher->nhisto)
                              histpos = 0;
                         strncpy(buf, launcher->histo[launcher->nhisto - ++histpos], sizeof(buf));
                         pos = strlen(buf);
                    }
                    break;
               case XK_Down:
                    if(launcher->nhisto && histpos > 0 && histpos < (int)launcher->nhisto)
                    {
                         strncpy(buf, launcher->histo[launcher->nhisto - --histpos], sizeof(buf));
                         pos = strlen(buf);
                    }
                    break;

               case XK_Return:
                    spawn("%s %s", launcher->command, buf);
                    /* Histo */
                    if(launcher->nhisto + 1 > HISTOLEN)
                    {
                         for(i = launcher->nhisto - 1; i > 1; --i)
                              strncpy(launcher->histo[i], launcher->histo[i - 1], sizeof(launcher->histo[i]));

                         launcher->nhisto = 0;
                    }
                    /* Store in histo array */
                    strncpy(launcher->histo[launcher->nhisto++], buf, sizeof(buf));

                    my_guitar_gently_wheeps = 0;
                    break;

               case XK_Escape:
                    my_guitar_gently_wheeps = 0;
                    break;

               case XK_Tab:
                    /*
                     * completion
                     * if there is not space in buffer we
                     * complete the command using complete_on_command.
                     * Else we try to complete on filename using
                     * complete_on_files.
                     */
                    buf[pos] = '\0';
                    if (lastwastab)
                         cache.hits++;
                    else
                    {
                         cache.hits = 0;
                         strncpy(tmpbuf, buf, sizeof(tmpbuf));
                    }


                    if (pos && (end = complete(&cache, tmpbuf))) {
                         strncpy(buf, tmpbuf, sizeof(buf));
                         strncat(buf, end, sizeof(buf));
                         found = True;
                    }

                    lastwastab = True;

                    /* start a new round of tabbing */
                    if (found == False)
                         cache.hits = 0;

                    pos = strlen(buf);

                    break;

               case XK_BackSpace:
                    lastwastab = False;
                    if(pos)
                         buf[--pos] = 0;
                    break;

               default:
                    lastwastab = False;
                    strncat(buf, tmp, sizeof(tmp));
                    ++pos;
                    break;
               }

               barwin_refresh_color(bw);

               /* Update cursor position */
               XSetForeground(dpy, gc, getcolor(infobar[selscreen].bar->fg));
               XDrawLine(dpy, bw->dr, gc,
                         1 + textw(launcher->prompt) + textw(" ") + textw(buf), 2,
                         1 + textw(launcher->prompt) + textw(" ") + textw(buf), INFOBARH - 4);

               barwin_draw_text(bw, 1, FHINFOBAR - 1, launcher->prompt);
               barwin_draw_text(bw, 1 + textw(launcher->prompt) + textw(" "), FHINFOBAR - 1, buf);
               barwin_refresh(bw);
          }
          else if(ev.type < nevent && ev.type > 0)
               HANDLE_EVENT(&ev);

          XNextEvent(dpy, &ev);
     }

     barwin_unmap(bw);
     barwin_delete(bw);
     infobar_draw(&infobar[selscreen]);
     complete_cache_free(&cache);

     XUngrabKeyboard(dpy, CurrentTime);

     return;

}

void
uicb_launcher(uicb_t cmd)
{
     int i;

     for(i = 0; i < conf.nlauncher; ++i)
          if(!strcmp(cmd, conf.launcher[i].name))
               launcher_execute(&conf.launcher[i]);

     return;
}
