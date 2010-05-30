// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2010 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "StdAfx.h"
#include "ShellCache.h"

ShellCache::ShellCache()
{
    cachetype = CRegStdDWORD(_T("Software\\TortoiseSVN\\CacheType"), GetSystemMetrics(SM_REMOTESESSION) ? dll : exe);
    showrecursive = CRegStdDWORD(_T("Software\\TortoiseSVN\\RecursiveOverlay"), TRUE);
    folderoverlay = CRegStdDWORD(_T("Software\\TortoiseSVN\\FolderOverlay"), TRUE);
    driveremote = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskRemote"));
    drivefixed = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskFixed"), TRUE);
    drivecdrom = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskCDROM"));
    driveremove = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskRemovable"));
    drivefloppy = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskFloppy"));
    driveram = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskRAM"));
    driveunknown = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskUnknown"));
    simplecontext = CRegStdDWORD(_T("Software\\TortoiseSVN\\SimpleContext"), FALSE);
    shellmenuaccelerators = CRegStdDWORD(_T("Software\\TortoiseSVN\\ShellMenuAccelerators"), TRUE);
    unversionedasmodified = CRegStdDWORD(_T("Software\\TortoiseSVN\\UnversionedAsModified"), FALSE);
    getlocktop = CRegStdDWORD(_T("Software\\TortoiseSVN\\GetLockTop"), TRUE);
    excludedasnormal = CRegStdDWORD(_T("Software\\TortoiseSVN\\ShowExcludedFoldersAsNormal"), FALSE);
    alwaysextended = CRegStdDWORD(_T("Software\\TortoiseSVN\\AlwaysExtendedMenu"), FALSE);
    cachetypeticker = GetTickCount();
    recursiveticker = cachetypeticker;
    folderoverlayticker = cachetypeticker;
    driveticker = cachetypeticker;
    drivetypeticker = 0;
    langticker = cachetypeticker;
    columnrevformatticker = cachetypeticker;
    pathfilterticker = 0;
    simplecontextticker = cachetypeticker;
    shellmenuacceleratorsticker = cachetypeticker;
    unversionedasmodifiedticker = cachetypeticker;
    columnseverywhereticker = cachetypeticker;
    getlocktopticker = cachetypeticker;
    excludedasnormalticker = cachetypeticker;
    alwaysextendedticker = cachetypeticker;
    excontextticker = 0;
    menulayoutlow = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntries"), MENUCHECKOUT | MENUUPDATE | MENUCOMMIT);
    menulayouthigh = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntrieshigh"), 0);
    menumasklow_lm = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskLow"), 0, FALSE, HKEY_LOCAL_MACHINE);
    menumaskhigh_lm = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskHigh"), 0, FALSE, HKEY_LOCAL_MACHINE);
    menumasklow_cu = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskLow"), 0);
    menumaskhigh_cu = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskHigh"), 0);
    langid = CRegStdDWORD(_T("Software\\TortoiseSVN\\LanguageID"), 1033);
    blockstatus = CRegStdDWORD(_T("Software\\TortoiseSVN\\BlockStatus"), 0);
    columnseverywhere = CRegStdDWORD(_T("Software\\TortoiseSVN\\ColumnsEveryWhere"), FALSE);
    std::fill_n(drivetypecache, 27, (UINT)-1);
    if (DWORD(drivefloppy) == 0)
    {
        // A: and B: are floppy disks
        drivetypecache[0] = DRIVE_REMOVABLE;
        drivetypecache[1] = DRIVE_REMOVABLE;
    }
    TCHAR szBuffer[5];
    columnrevformatticker = GetTickCount();
    SecureZeroMemory(&columnrevformat, sizeof(NUMBERFMT));
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, &szDecSep[0], sizeof(szDecSep));
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, &szThousandsSep[0], sizeof(szThousandsSep));
    columnrevformat.lpDecimalSep = szDecSep;
    columnrevformat.lpThousandSep = szThousandsSep;
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
    columnrevformat.Grouping = _ttoi(szBuffer);
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_INEGNUMBER, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
    columnrevformat.NegativeOrder = _ttoi(szBuffer);
    nocontextpaths = CRegStdString(_T("Software\\TortoiseSVN\\NoContextPaths"), _T(""));
    m_critSec.Init();
}

void ShellCache::ForceRefresh()
{
    cachetype.read();
    showrecursive.read();
    folderoverlay.read();
    driveremote.read();
    drivefixed.read();
    drivecdrom.read();
    driveremove.read();
    drivefloppy.read();
    driveram.read();
    driveunknown.read();
    simplecontext.read();
    shellmenuaccelerators.read();
    unversionedasmodified.read();
    excludedasnormal.read();
    alwaysextended.read();
    menulayoutlow.read();
    menulayouthigh.read();
    langid.read();
    blockstatus.read();
    columnseverywhere.read();
    getlocktop.read();
    menumasklow_lm.read();
    menumaskhigh_lm.read();
    menumasklow_cu.read();
    menumaskhigh_cu.read();
    nocontextpaths.read();

    pathFilter.Refresh();
}

ShellCache::CacheType ShellCache::GetCacheType()
{
    if ((GetTickCount() - cachetypeticker) > REGISTRYTIMEOUT)
    {
        cachetypeticker = GetTickCount();
        cachetype.read();
    }
    return CacheType(DWORD((cachetype)));
}

DWORD ShellCache::BlockStatus()
{
    if ((GetTickCount() - blockstatusticker) > REGISTRYTIMEOUT)
    {
        blockstatusticker = GetTickCount();
        blockstatus.read();
    }
    return (blockstatus);
}

unsigned __int64 ShellCache::GetMenuLayout()
{
    if ((GetTickCount() - layoutticker) > REGISTRYTIMEOUT)
    {
        layoutticker = GetTickCount();
        menulayoutlow.read();
        menulayouthigh.read();
    }
    unsigned __int64 temp = unsigned __int64(DWORD(menulayouthigh))<<32;
    temp |= unsigned __int64(DWORD(menulayoutlow));
    return temp;
}

unsigned __int64 ShellCache::GetMenuMask()
{
    if ((GetTickCount() - menumaskticker) > REGISTRYTIMEOUT)
    {
        menumaskticker = GetTickCount();
        menumasklow_lm.read();
        menumaskhigh_lm.read();
        menumasklow_cu.read();
        menumaskhigh_cu.read();
    }
    DWORD low = (DWORD)menumasklow_lm | (DWORD)menumasklow_cu;
    DWORD high = (DWORD)menumaskhigh_lm | (DWORD)menumaskhigh_cu;
    unsigned __int64 temp = unsigned __int64(high)<<32;
    temp |= unsigned __int64(low);
    return temp;
}

BOOL ShellCache::IsRecursive()
{
    if ((GetTickCount() - recursiveticker)>REGISTRYTIMEOUT)
    {
        recursiveticker = GetTickCount();
        showrecursive.read();
    }
    return (showrecursive);
}

BOOL ShellCache::IsFolderOverlay()
{
    if ((GetTickCount() - folderoverlayticker)>REGISTRYTIMEOUT)
    {
        folderoverlayticker = GetTickCount();
        folderoverlay.read();
    }
    return (folderoverlay);
}

BOOL ShellCache::IsSimpleContext()
{
    if ((GetTickCount() - simplecontextticker)>REGISTRYTIMEOUT)
    {
        simplecontextticker = GetTickCount();
        simplecontext.read();
    }
    return (simplecontext!=0);
}

BOOL ShellCache::HasShellMenuAccelerators()
{
    if ((GetTickCount() - shellmenuacceleratorsticker)>REGISTRYTIMEOUT)
    {
        shellmenuacceleratorsticker = GetTickCount();
        shellmenuaccelerators.read();
    }
    return (shellmenuaccelerators!=0);
}

BOOL ShellCache::IsUnversionedAsModified()
{
    if ((GetTickCount() - unversionedasmodifiedticker)>REGISTRYTIMEOUT)
    {
        unversionedasmodifiedticker = GetTickCount();
        unversionedasmodified.read();
    }
    return (unversionedasmodified);
}

BOOL ShellCache::IsGetLockTop()
{
    if ((GetTickCount() - getlocktopticker)>REGISTRYTIMEOUT)
    {
        getlocktopticker = GetTickCount();
        getlocktop.read();
    }
    return (getlocktop);
}

BOOL ShellCache::ShowExcludedAsNormal()
{
    if ((GetTickCount() - excludedasnormalticker)>REGISTRYTIMEOUT)
    {
        excludedasnormalticker = GetTickCount();
        excludedasnormal.read();
    }
    return (excludedasnormal);
}

BOOL ShellCache::AlwaysExtended()
{
    if ((GetTickCount() - alwaysextendedticker)>REGISTRYTIMEOUT)
    {
        alwaysextendedticker = GetTickCount();
        alwaysextended.read();
    }
    return (alwaysextended);
}

BOOL ShellCache::IsRemote()
{
    DriveValid();
    return (driveremote);
}

BOOL ShellCache::IsFixed()
{
    DriveValid();
    return (drivefixed);
}

BOOL ShellCache::IsCDRom()
{
    DriveValid();
    return (drivecdrom);
}

BOOL ShellCache::IsRemovable()
{
    DriveValid();
    return (driveremove);
}

BOOL ShellCache::IsRAM()
{
    DriveValid();
    return (driveram);
}

BOOL ShellCache::IsUnknown()
{
    DriveValid();
    return (driveunknown);
}

BOOL ShellCache::IsContextPathAllowed(LPCTSTR path)
{
    Locker lock(m_critSec);
    ExcludeContextValid();
    for (std::vector<tstring>::iterator I = excontextvector.begin(); I != excontextvector.end(); ++I)
    {
        if (I->empty())
            continue;
        if (I->size() && I->at(I->size()-1)=='*')
        {
            tstring str = I->substr(0, I->size()-1);
            if (_tcsnicmp(str.c_str(), path, str.size())==0)
                return FALSE;
        }
        else if (_tcsicmp(I->c_str(), path)==0)
            return FALSE;
    }
    return TRUE;
}

BOOL ShellCache::IsPathAllowed(LPCTSTR path)
{
    ValidatePathFilter();
    Locker lock(m_critSec);
    svn_tristate_t allowed = pathFilter.IsPathAllowed (path);
    if (allowed != svn_tristate_unknown)
        return allowed == svn_tristate_true ? TRUE : FALSE;

    UINT drivetype = 0;
    int drivenumber = PathGetDriveNumber(path);
    if ((drivenumber >=0)&&(drivenumber < 25))
    {
        drivetype = drivetypecache[drivenumber];
        if ((drivetype == -1)||((GetTickCount() - drivetypeticker)>DRIVETYPETIMEOUT))
        {
            if ((DWORD(drivefloppy) == 0)&&((drivenumber == 0)||(drivenumber == 1)))
                drivetypecache[drivenumber] = DRIVE_REMOVABLE;
            else
            {
                drivetypeticker = GetTickCount();
                TCHAR pathbuf[MAX_PATH+4];      // MAX_PATH ok here. PathStripToRoot works with partial paths too.
                _tcsncpy_s(pathbuf, MAX_PATH+4, path, MAX_PATH+3);
                PathStripToRoot(pathbuf);
                PathAddBackslash(pathbuf);
                ATLTRACE2(_T("GetDriveType for %s, Drive %d\n"), pathbuf, drivenumber);
                drivetype = GetDriveType(pathbuf);
                drivetypecache[drivenumber] = drivetype;
            }
        }
    }
    else
    {
        TCHAR pathbuf[MAX_PATH+4];      // MAX_PATH ok here. PathIsUNCServer works with partial paths too.
        _tcsncpy_s(pathbuf, MAX_PATH+4, path, MAX_PATH+3);
        if (PathIsUNCServer(pathbuf))
            drivetype = DRIVE_REMOTE;
        else
        {
            PathStripToRoot(pathbuf);
            PathAddBackslash(pathbuf);
            if (_tcsncmp(pathbuf, drivetypepathcache, MAX_PATH-1)==0)       // MAX_PATH ok.
                drivetype = drivetypecache[26];
            else
            {
                ATLTRACE2(_T("GetDriveType for %s\n"), pathbuf);
                drivetype = GetDriveType(pathbuf);
                drivetypecache[26] = drivetype;
                _tcsncpy_s(drivetypepathcache, MAX_PATH, pathbuf, MAX_PATH);            // MAX_PATH ok.
            }
        }
    }
    if ((drivetype == DRIVE_REMOVABLE)&&(!IsRemovable()))
        return FALSE;
    if ((drivetype == DRIVE_FIXED)&&(!IsFixed()))
        return FALSE;
    if (((drivetype == DRIVE_REMOTE)||(drivetype == DRIVE_NO_ROOT_DIR))&&(!IsRemote()))
        return FALSE;
    if ((drivetype == DRIVE_CDROM)&&(!IsCDRom()))
        return FALSE;
    if ((drivetype == DRIVE_RAMDISK)&&(!IsRAM()))
        return FALSE;
    if ((drivetype == DRIVE_UNKNOWN)&&(IsUnknown()))
        return FALSE;

    return TRUE;
}

DWORD ShellCache::GetLangID()
{
    if ((GetTickCount() - langticker) > REGISTRYTIMEOUT)
    {
        langticker = GetTickCount();
        langid.read();
    }
    return (langid);
}

NUMBERFMT * ShellCache::GetNumberFmt()
{
    if ((GetTickCount() - columnrevformatticker) > NUMBERFMTTIMEOUT)
    {
        TCHAR szBuffer[5];
        columnrevformatticker = GetTickCount();
        SecureZeroMemory(&columnrevformat, sizeof(NUMBERFMT));
        GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, &szDecSep[0], sizeof(szDecSep));
        GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, &szThousandsSep[0], sizeof(szThousandsSep));
        columnrevformat.lpDecimalSep = szDecSep;
        columnrevformat.lpThousandSep = szThousandsSep;
        GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
        columnrevformat.Grouping = _ttoi(szBuffer);
        GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_INEGNUMBER, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
        columnrevformat.NegativeOrder = _ttoi(szBuffer);
    }
    return &columnrevformat;
}

BOOL ShellCache::HasSVNAdminDir(LPCTSTR path, BOOL bIsDir)
{
    tstring folder (path);
    if (! bIsDir)
    {
        size_t pos = folder.rfind ('\\');
        if (pos != tstring::npos)
            folder.erase (pos);
    }
    std::map<tstring, BoolTimeout>::iterator iter;
    if ((iter = admindircache.find(folder)) != admindircache.end())
    {
        if ((GetTickCount() - iter->second.timeout) < ADMINDIRTIMEOUT)
            return iter->second.bBool;
    }

    BoolTimeout bt;
    bt.bBool = g_SVNAdminDir.HasAdminDir (folder.c_str(), true);
    bt.timeout = GetTickCount();
    Locker lock(m_critSec);
    admindircache[folder] = bt;
    return bt.bBool;
}

bool ShellCache::IsColumnsEveryWhere()
{
    if ((GetTickCount() - columnseverywhereticker) > REGISTRYTIMEOUT)
    {
        columnseverywhereticker = GetTickCount();
        columnseverywhere.read();
    }
    return !!(DWORD)columnseverywhere;
}

void ShellCache::DriveValid()
{
    if ((GetTickCount() - driveticker)>REGISTRYTIMEOUT)
    {
        driveticker = GetTickCount();
        driveremote.read();
        drivefixed.read();
        drivecdrom.read();
        driveremove.read();
        drivefloppy.read();
    }
}

void ShellCache::ExcludeContextValid()
{
    if ((GetTickCount() - excontextticker)>EXCLUDELISTTIMEOUT)
    {
        Locker lock(m_critSec);
        excontextticker = GetTickCount();
        nocontextpaths.read();
        if (excludecontextstr.compare((tstring)nocontextpaths)==0)
            return;
        excludecontextstr = (tstring)nocontextpaths;
        excontextvector.clear();
        size_t pos = 0, pos_ant = 0;
        pos = excludecontextstr.find(_T("\n"), pos_ant);
        while (pos != tstring::npos)
        {
            tstring token = excludecontextstr.substr(pos_ant, pos-pos_ant);
            if (!token.empty())
                excontextvector.push_back(token);
            pos_ant = pos+1;
            pos = excludecontextstr.find(_T("\n"), pos_ant);
        }
        if (!excludecontextstr.empty())
        {
            tstring token = excludecontextstr.substr(pos_ant, excludecontextstr.size()-1);
            if (!token.empty())
                excontextvector.push_back(token);
        }
        excludecontextstr = (tstring)nocontextpaths;
    }
}

void ShellCache::ValidatePathFilter()
{
    DWORD ticks = GetTickCount();
    if ((ticks - pathfilterticker) > EXCLUDELISTTIMEOUT)
    {
        Locker lock(m_critSec);

        pathfilterticker = ticks;
        pathFilter.Refresh();
    }
}

// construct \ref data content

void ShellCache::CPathFilter::AddEntry (const tstring& s, bool include)
{
    if (s.empty())
        return;

    TCHAR lastChar = *s.rbegin();

    SEntry entry;
    entry.hasSubFolderEntries = false;
    entry.recursive = lastChar != _T('?');
    entry.included = include ? svn_tristate_true : svn_tristate_false;
    entry.subPathIncluded = include == entry.recursive
                          ? svn_tristate_true 
                          : svn_tristate_false;

    entry.path = s;
    if ((lastChar == _T('?')) || (lastChar == _T('*')))
        entry.path.erase (s.length()-1);
    if (!entry.path.empty() && (*entry.path.rbegin() == _T('\\')))
        entry.path.erase (entry.path.length()-1);

    data.push_back (entry);
}

void ShellCache::CPathFilter::AddEntries (const tstring& s, bool include)
{
    size_t pos = 0, pos_ant = 0;
    pos = s.find(_T('\n'), pos_ant);
    while (pos != tstring::npos)
    {
        AddEntry (s.substr(pos_ant, pos-pos_ant), include);
        pos_ant = pos+1;
        pos = s.find(_T('\n'), pos_ant);
    }

    if (!s.empty())
        AddEntry (s.substr(pos_ant, s.size()-1), include);
}

// for all paths, have at least one entry in data

void ShellCache::CPathFilter::PostProcessData()
{
    if (data.empty())
        return;

    std::sort (data.begin(), data.end());

    // update subPathIncluded props and remove duplicate entries

    TData::iterator begin = data.begin();
    TData::iterator end = data.end();
    TData::iterator dest = begin;
    for (TData::iterator source = begin; source != end; ++source)
    {
        if (_tcsicmp (source->path.c_str(), dest->path.c_str()) == 0)
        {
            // multiple entries for the same path -> merge them

            // update subPathIncluded
            // (all relevant parent info has already been normalized)

            if (!source->recursive)
                source->subPathIncluded
                    = IsPathAllowed (source->path.c_str(), begin, dest);

            // multiple specs for the same path
            // -> merge them into the existing entry @ dest

            if (!source->recursive && dest->recursive)
            {
                // reset the marker for the this case

                dest->recursive = false;
                dest->included = source->included;
            }
            else
            {
                // include beats exclude

                if (source->included == svn_tristate_true)
                    dest->included = svn_tristate_true;
                if (   source->recursive 
                    && source->subPathIncluded == svn_tristate_true)
                    dest->subPathIncluded = svn_tristate_true;
            }
        }
        else
        {
            // new path -> don't merge this entry

            size_t destSize = dest->path.size();
            dest->hasSubFolderEntries
                =   (source->path.size() > destSize)
                 && (source->path[destSize] == _T('\\'))
                 && (_tcsnicmp ( source->path.substr (0, destSize).c_str()
                               , dest->path.c_str()
                               , destSize)
                     == 0);

            *++dest = *source;

            // update subPathIncluded
            // (all relevant parent info has already been normalized)

            if (!dest->recursive)
                dest->subPathIncluded
                    = IsPathAllowed (source->path.c_str(), begin, dest);
        }
    }

    // remove duplicate info

    data.erase (++dest, end);
}

// lookup. default result is "unknown".
// We must look for *every* parent path because of situations like:
// excluded: C:, C:\some\deep\path
// include: C:\some
// lookup for C:\some\deeper\path

svn_tristate_t ShellCache::CPathFilter::IsPathAllowed
    ( LPCTSTR path
    , TData::const_iterator begin
    , TData::const_iterator end) const
{
    svn_tristate_t result = svn_tristate_unknown;

    // handle special cases

    if (begin == end)
        return result;

    size_t maxLength = _tcslen (path);
    if (maxLength == 0)
        return result;

    // look for the most specific entry, start at the root

    size_t pos = 0;
    do
    {
        LPCTSTR backslash = _tcschr (path + pos + 1, _T ('\\'));
        pos = backslash == NULL ? maxLength : backslash - path;

        std::pair<LPCTSTR, size_t> toFind (path, pos);
        TData::const_iterator iter
            = std::lower_bound (begin, end, toFind);

        // found a relevant entry?

        if (   (iter != end)
            && (iter->path.length() == pos)
            && (_tcsnicmp (iter->path.c_str(), path, pos) == 0))
        {
            // exact match?

            if (pos == maxLength)
                return iter->included;

            // parent match

            result = iter->subPathIncluded;

            // done?

            if (iter->hasSubFolderEntries)
                begin = iter;
            else
                return result;
        }
        else
        {
            // set a (potentially) closer lower limit

            if (iter != begin)
                begin = --iter;
        }

        // set a (potentially) closer upper limit

        end = std::upper_bound (begin, end, toFind);
    }
    while ((pos < maxLength) && (begin != end));

    // nothing more specific found

    return result;
}

// construction

ShellCache::CPathFilter::CPathFilter()
    : excludelist (_T("Software\\TortoiseSVN\\OverlayExcludeList"))
    , includelist (_T("Software\\TortoiseSVN\\OverlayIncludeList"))
{
    Refresh();
}

// notify of (potential) registry settings

void ShellCache::CPathFilter::Refresh()
{
    excludelist.read();
    includelist.read();

    if (   (excludeliststr.compare ((tstring)excludelist)==0)
        && (includeliststr.compare ((tstring)includelist)==0))
    {
        return;
    }

    excludeliststr = (tstring)excludelist;
    includeliststr = (tstring)includelist;
    AddEntries (excludeliststr, false);
    AddEntries (includeliststr, true);

    PostProcessData();
}

// data access

svn_tristate_t ShellCache::CPathFilter::IsPathAllowed (LPCTSTR path) const
{
    return path != NULL
        ? IsPathAllowed (path, data.begin(), data.end())
        : svn_tristate_unknown;
}

