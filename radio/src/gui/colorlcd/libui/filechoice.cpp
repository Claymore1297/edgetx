/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   libopenui - https://github.com/opentx/libopenui
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "filechoice.h"

#include <algorithm>

#include "dialog.h"
#include "edgetx.h"
#include "lib_file.h"
#include "menu.h"
#include "menutoolbar.h"

static bool fileExists(const std::string &path)
{
  FILINFO fno;
  return f_stat(path.c_str(), &fno) == FR_OK;
}

static std::string getCsvField(const std::string &line, int index)
{
  std::vector<std::string> fields;
  std::string current;
  bool inQuotes = false;

  for (char ch : line)
  {
    if (ch == '"')
    {
      inQuotes = !inQuotes;
    }
    else if (ch == ',' && !inQuotes)
    {
      fields.push_back(current);
      current.clear();
    }
    else
    {
      current += ch;
    }
  }
  fields.push_back(current);

  if (index < 0 || index >= (int)fields.size())
    return "";

  std::string f = fields[index];
  f.erase(std::remove(f.begin(), f.end(), '"'), f.end());

  return f;
}

class FileChoiceMenuToolbar : public MenuToolbar
{
 public:
  FileChoiceMenuToolbar(FileChoice *choice, Menu *menu) :
      MenuToolbar(choice, menu, FC_COLS)
  {
    filterButton(choice, 'a', 'd', "aA-dD");
    filterButton(choice, 'e', 'h', "eE-hH");
    filterButton(choice, 'i', 'l', "iI-lL");
    filterButton(choice, 'm', 'p', "mM-pP");
    filterButton(choice, 'q', 't', "qQ-tT");
    filterButton(choice, 'u', 'z', "uU-zZ");
    filterButton(choice, '0', '9', "0-9");

    bool found = false;
    for (int i = 0; i <= choice->getMax(); i++) {
      char c = choice->getString(i)[0];
      if (c && !isdigit(c) && !isalpha(c)) {
        found = true;
        break;
      }
    }

    if (found) {
      addButton(
          "._-", 0, choice->getMax(),
          [=](int16_t index) {
            char c = choice->getString(index)[0];
            return c && !isdigit(c) && !isalpha(c);
          },
          STR_MENU_OTHER);
    }

    addButton(STR_SELECT_MENU_CLR, 0, 0, nullptr, nullptr, true);
  }

  void filterButton(FileChoice *choice, char from, char to, const char *title)
  {
    bool found = false;
    for (int i = 0; i <= choice->getMax(); i++) {
      char c = choice->getString(i)[0];
      if (isupper(c)) c += 0x20;
      if (c >= from && c <= to) {
        found = true;
        break;
      }
    }

    if (found) {
      char s[4];
      s[0] = from; s[1] = '-'; s[2] = to; s[3] = 0;
      addButton(
          s, 0, choice->getMax(),
          [=](int16_t index) {
            char c = choice->getString(index)[0];
            if (isupper(c)) c += 0x20;
            return (c >= from && c <= to);
          },
          title);
    }
  }

  static LAYOUT_SIZE(FC_COLS, 3, 2)
};

FileChoice::FileChoice(Window *parent, const rect_t &rect, const std::string folder,
                       const std::string extension, int maxlen,
                       std::function<std::string()> getValue,
                       std::function<void(std::string)> setValue,
                       bool stripExtension, const char *title) :
    Choice(
        parent, rect, 0, 0, [=]() { return selectedIdx >= 0 ? selectedIdx : 0; },
        [=](int val) {
          if (val >= 0 && val < (int)entries.size())
            setValue(entries[val].second);
          selectedIdx = val;
        },
        title, CHOICE_TYPE_FOLDER),
    folder(std::move(folder)),
    extension(std::move(extension)),
    maxlen(maxlen),
    getValue(std::move(getValue)),
    stripExtension(stripExtension)
{
  update();
}

std::string FileChoice::getLabelText() { return getValue(); }

void FileChoice::loadFiles()
{
  if (filesLoaded) return;

  filesLoaded = true;

  entries.clear();

  std::string csvPath = "/" + folder + "/"+ std::string(currentLanguagePack->id, 2) + ".csv";

  bool useCsv = fileExists(csvPath);

  if (useCsv)
  {
    FIL file;
    if (f_open(&file, csvPath.c_str(), FA_READ) == FR_OK)
    {
      char line[256];
      int pos = 0;
      char c;
      UINT br;

      while (true)
      {
        if (f_read(&file, &c, 1, &br) != FR_OK || br != 1)
          break;

        if (c == '\n' || pos >= (int)sizeof(line) - 1)
        {
          line[pos] = '\0';
          pos = 0;

          std::string str(line);
          str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());

          if (!str.empty())
          {
            // col 3 = display name, col 6 =raw file
            std::string display = getCsvField(str, 2);
            std::string raw     = getCsvField(str, 5);

            if (!display.empty() && !raw.empty())
            {
              std::string fullPath = "/" + folder + "/" + raw;

              if (fileExists(fullPath))
                entries.emplace_back(display, raw);
            }
          }
        }
        else
        {
          line[pos++] = c;
        }
      }

      f_close(&file);
    }
  }

  if (!useCsv || entries.empty()) {
    DIR dir;
    FILINFO fno;
    std::list<std::string> files;

    if (f_opendir(&dir, ("/" + folder).c_str()) == FR_OK)
    {
      for (;;)
      {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
          break;

        if (fno.fattrib & (AM_HID | AM_SYS | AM_DIR))
          continue;

        if (fno.fname[0] == '.' && fno.fname[1] != '.')
          continue;

        const char *fnExt;
        uint8_t fnLen, extLen;

        fnExt = getFileExtension(fno.fname, 0, 0, &fnLen, &extLen);

        if (!extension.empty() &&
            (!fnExt || !isExtensionMatching(fnExt, extension.c_str())))
          continue;

        if (stripExtension)
          fnLen -= extLen;

        if (!fnLen || fnLen > maxlen)
          continue;

        std::string name(fno.fname, fnLen);
        files.push_back(name);
      }

      f_closedir(&dir);
    }

    for (auto &f : files)
      entries.emplace_back(f, f);
  }

    if (entries.empty())
    return;

  std::sort(entries.begin(), entries.end(),
            [](const auto &a, const auto &b) {
              return compare_nocase(a.first, b.first);
            });

  entries.insert(entries.begin(), {"", ""});

  std::string current = getValue();
  int idx = 0;

  for (const auto &e : entries)
  {
    addValue(e.first.c_str());

    if (!current.empty() && current == e.second)
      selectedIdx = idx;

    idx++;
  }

  setMax(entries.size() - 1);
  fileCount = entries.size();
}

void FileChoice::openMenu()
{
  loadFiles();

  if (fileCount > 0) {
    setEditMode(true);  // this needs to be done first before menu is created.

    auto menu = new Menu();
    if (menuTitle) menu->setTitle(menuTitle);

    auto tb = new FileChoiceMenuToolbar(this, menu);
    menu->setToolbar(tb);

    // fillMenu(menu); - called by MenuToolbar

    menu->setCloseHandler([=]() { setEditMode(false); });
  } else {
    new MessageDialog(STR_SDCARD, STR_NO_FILES_ON_SD);
  }
  if (entries.empty()) {
    selectedIdx = 0;
    return;
  }
}