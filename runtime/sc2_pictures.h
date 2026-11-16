/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Export pictures (the menu's Pictures page, issue #82): the Art Gallery's pictures as JPEG files. */
#ifndef SC2_PICTURES_H
#define SC2_PICTURES_H

#include <string>

namespace sc2 {

/* The root.olk the game reads in place of the disc's (the guests' file), or "" for the disc's own. The runner says
   which once it has put it in place (recomp_disc_override). */
void SetPicturesRootOlk(const std::string &host_path);

/* Where Export pictures writes: pictures/ beside settings.ini (the launcher's YOUR FILES), or in the current folder
   with no settings file. */
std::string PicturesFolder(const std::string &settings_path);

/* Every picture the Art Gallery has (its three galleries as the program's table has them now, the guests' with them)
   written to `folder` as <gallery>-<NN>.jpg: the number of files written, or -1 with `error` saying why. */
int ExportPictures(const std::string &folder, std::string &error);

/* The folder in the system's file manager */
void OpenFolder(const std::string &folder);

} // namespace sc2

#endif
