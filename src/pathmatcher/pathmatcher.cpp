//==================================================================================================
// PathMatch.cpp
//
//     Routines for matching wildcard path specifications against a given directory tree.
//
// Copyright 2013 Steve Hollasch
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under
// the License.
//==================================================================================================

#include "pathmatcher.h"

#include <io.h>
#include <string.h>
#include <windows.h>

#include <stdio.h>
#include <assert.h>


namespace PathMatch {


static const wchar_t c_slash = L'\\';



//==============================================================================

PathMatcher::PathMatcher ()
  : m_callback (NULL),
    m_cbdata (NULL),
    m_dirsonly (false),
    m_ellpath (NULL),
    m_ellpattern (NULL),
    m_pattern (NULL),
    m_pattern_buff_size (0)
{
    //--------------------------------------------------------------------------
    // PathMatcher Default Constructor
    //--------------------------------------------------------------------------

    m_path[0] = 0;
}

//==============================================================================

PathMatcher::~PathMatcher ()
{
    //--------------------------------------------------------------------------
    // PathMatcher Destructor
    //--------------------------------------------------------------------------

    delete m_pattern;
}

//==============================================================================

wchar_t* PathMatcher::AppendPath (wchar_t *pathend, const wchar_t *str)
{
    //--------------------------------------------------------------------------
    // This procedure appends the current path with the specified string.
    //
    // Parameter 'pathend' is the end of the current path (one past the last
    // character). Parameter 'str' is the string to append.
    //
    // This function  returns the new path end pointer, or null if the path
    // buffer is not large enough to append the new entry name.
    //--------------------------------------------------------------------------

    const size_t strlength = wcslen (str);

    // Return NULL if there's not enough space to append the string.

    if (PathSpaceLeft(pathend) < (strlength + 1))
        return NULL;

    while (*str)
        *pathend++ = *str++;

    *pathend = 0;

    return pathend;
}

//==============================================================================

bool PathMatcher::AllocPatternBuff (size_t requested_size)
{
    //--------------------------------------------------------------------------
    // This function allocates, if necessary, the memory for the pattern buffer.
    // If the size requested is already accomodated by the pattern buffer, no
    // action is taken.
    //
    // Parameter 'requested_size' is the total buffer size needed, including the
    // string termination token.
    //
    // This function returns true if the buffer is ready, or false if the
    // necessary memory could not be allocated.
    //--------------------------------------------------------------------------

    if (requested_size > m_pattern_buff_size)
    {
        delete m_pattern;

        m_pattern = new wchar_t [requested_size];

        if (!m_pattern)
        {   m_pattern_buff_size = 0;
            return false;
        }

        m_pattern_buff_size = requested_size;
    }

    return true;
}

//==============================================================================

bool PathMatcher::CopyGroomedPattern (const wchar_t *pattern)
{
    //--------------------------------------------------------------------------
    // This routine copies the given pattern into the m_pattern member field.
    // While doing so, it collapses sequences of repeating slashes, eliminates
    // "/./" subpaths, resolves parent subpaths ("/../"), and determines if a
    // directory pattern (trailing slash) was specified.
    //
    // The parameter 'pattern' is the original caller-supplied pattern.
    //
    // This function returns false if this routine encounted an out-of-memory
    // error.
    //--------------------------------------------------------------------------

    // Allocate the buffer needed to store the pattern.

    if (!AllocPatternBuff (wcslen(pattern) + 1))
        return false;

    const wchar_t* src  = pattern;
          wchar_t* dest = m_pattern;

    // Preserve leading multiple slashes at the beginning of the pattern.

    while (IsSlash(*src))
        *dest++ = *src++;

    // Now copy the remainder of the path. Eliminate "." subpaths, reduce
    // repeating slashes to single slashes, and resolve ".." portions.

    while (*src)
    {
        if ((src[0] == L'.') && ((src[1] == 0) || IsSlash(src[1])))
        {
            // The current subpath is a '.' directory.

            const bool atStart = (dest == m_pattern);

            // Skip past any trailing slashes

            do { ++ src; } while (IsSlash(*src));

            if (atStart)
            {
                // If the pattern is just "." or "./" (for any number of
                // tailing slashes), then just use "." as the pattern. If it is
                // just prefixed with "./", then skip that and continue.

                if (*src == 0)
                {   *dest++ = L'.';
                    m_dirsonly = true;
                }
            }
            else if (*src == 0)
            {
                // If the pattern ends in "." or "./" (for any number of
                // trailing slashes), then flag the search as directories-only
                // and zap the prior slash.

                --dest;
                m_dirsonly = true;
            }
            else
            {
                // We've encountered a "./" in the middle of a path. In this
                // case, just skip the copy.
            }
        }
        else if (IsSlash(*src))
        {
            if (src[1] == 0)          // If the pattern ends in a slash, then
            {                         // record that the pattern is specifying
                m_dirsonly = true;    // directories only.
                ++src;
            }
            else                      // Copy one slash only.
            {
                *dest++ = c_slash;
                do { ++src; } while (IsSlash(*src));
            }
        }
        else if (IsUpDir(src))
        {
            // If we encounter a "../" in the middle of a pattern, then erase
            // the prior parent directory if possible, otherwise append the
            // "../" substring.

            // Skip forward in the source string past all trailing slashes.

            for (src+=3;  IsSlash(*src);  ++src)
                continue;

            const size_t destlen = dest - m_pattern;   // Current pattern length
            wchar_t* parent = NULL;                 // Candidate parent portion

            if ((destlen >= 2) && IsSlash(dest[-1]) && !IsSlash(dest[-2]))
            {
                parent = dest - 2;

                // Scan backwards to the beginning of the parent directory.

                while ((parent > m_pattern) && !IsSlash(*parent))
                    --parent;

                // Move past the prior leading slash if necessary (if the parent
                // directory isn't the first subdirectory in the path).

                if (IsSlash(*parent)) ++parent;

                // If the parent directory is already a "../", then just append
                // the current up directory to the last one.

                if (IsUpDir(parent))
                    parent = NULL;
            }

            if (parent)
                dest = parent;
            else
            {
                *dest++ = L'.';
                *dest++ = L'.';
                *dest++ = c_slash;
            }
        }
        else
        {
            // If no special cases, then just copy up till the next slash or end
            // of pattern.

            while (*src && !IsSlash(*src))
                *dest++ = *src++;
        }
    }

    assert (!IsSlash(dest[-1]));

    *dest = 0;

    return true;
}

//==============================================================================

bool PathMatcher::Match (
    const wchar_t*     path_pattern,
    MatchTreeCallback* callback_func,
    void*              userdata)
{
    //--------------------------------------------------------------------------
    // This function walks a directory tree according to the given wildcard
    // pattern, and calls the specified callback function for each matching
    // entry.
    //
    // 'path_pattern' is the pattern to match against tree entries.
    // 'callback_func' is the callback function for each matching entry.
    // 'userdata' is the user data to be passed along to callback function.
    //
    // This function returns true if the function successfully completes the
    // search, otherwise false.
    //--------------------------------------------------------------------------

    if (!callback_func)      // Bail out if the user didn't provide a
        return false;        // callback function.

    m_callback = callback_func;
    m_cbdata = userdata;

    // Copy the groomed pattern (see comments for CopyGroomedPattern) into the
    // appropriate member fields.

    if (!CopyGroomedPattern(path_pattern))
        return false;

    // We will divide the path_pattern up into two parts: the root path, and the
    // remaining pattern. For example, "C:/foo/.../bar*" would be divided up
    // into a root of "C:/foo" and a pattern of ".../bar*".

    wchar_t *rootend   = m_pattern;
    wchar_t *wildstart = m_pattern;
    wchar_t *ptr       = m_pattern;

    // Locate the end of the root portion of the file pattern, and the start of
    // the wildcard pattern.

    for (; *ptr; ++ptr)
    {
        if (IsSlash(*ptr) || (*ptr == L':'))
        {
            rootend   = ptr;
            wildstart = ptr + 1;
        }
        else if ((*ptr == L'?') || IsMultiWildStr(ptr))
        {
            break;
        }
    }

    // If the supplied pattern has no specific root directory, then just set the
    // root directory to the current directory.

    size_t rootlen;    // Length of the root path string.

    if (rootend == m_pattern)
    {
        m_path[0] = 0;
        rootlen = 0;
    }
    else
    {
        ++rootend;                // Include the '/' or ':' character.

        rootlen = rootend - m_pattern;

        if (FAILED(wcsncpy_s (m_path, _countof(m_path), m_pattern, rootlen)))
            return false;
    }

    MatchDir (m_path + rootlen, wildstart);

    return true;
}

//==============================================================================

void PathMatcher::MatchDir (
    wchar_t*       pathend,
    const wchar_t* pattern)
{
    //--------------------------------------------------------------------------
    // This procedure matches a substring pattern against a given root
    // directory. Each matching entry in the tree will yield a call back to the
    // specified function, along with given user data. Note that the path string
    // buffer will be used to pass back matching entries to the callback
    // function.
    //
    // 'pathend' is the end of the current path (one past the last character)
    // 'pattern is the pattern against which to match directory entries.
    //--------------------------------------------------------------------------

    // If the pattern is null, then just return.

    if (!pattern || !*pattern) return;

    // Characterize the type of pattern matching we'll be doing in the current
    // directory. Scan forward to find the first of the end of the pattern, a
    // slash, or an ellipsis.

    int  ipatt = 0;
    bool fliteral = true;

    while (  pattern[ipatt]
          && !IsSlash(pattern[ipatt])
          && !IsEllipsis(pattern + ipatt))
    {
        if ((pattern[ipatt] == L'?') || (pattern[ipatt] == L'*'))
            fliteral = false;

        ++ ipatt;
    }

    // If the current pattern subdirectory contains an ellipsis, then handle the
    // remainder of the pattern and return.

    if (IsEllipsis(pattern + ipatt))
    {
        HandleEllipsisSubpath (pathend, pattern, ipatt);
        return;
    }

    assert (!pattern[ipatt] || IsSlash(pattern[ipatt]));

    const bool fdirmatch = IsSlash(pattern[ipatt]);
    const bool fdescend  = fdirmatch && (pattern[ipatt+1] != 0);

    // (simple) (end)

    wchar_t *subpattern = new wchar_t [ipatt+1];

    if (!subpattern) return;   // Bail out if out of memory.

    if (FAILED(wcsncpy_s (subpattern, ipatt+1, pattern, ipatt)))
    {   delete subpattern;
        return;
    }

    // If we have a literal subdirectory name (or filename), then just provide
    // that name to the find-file functions.

    errno_t retval = S_OK;    // General Return Value

    if (fliteral)
        retval = wcsncpy_s (pathend, PathSpaceLeft(pathend), pattern, ipatt);

    // If there's a wildcard subdirectory or file name, then enumerate all
    // directory entries and filter the results.

    if (!fliteral || FAILED(retval))
    {   pathend[0] = L'*';
        pathend[1] = 0;
    }

    WIN32_FIND_DATA finddata;
    HANDLE find_handle = FindFirstFile (m_path, &finddata);

    if (find_handle != INVALID_HANDLE_VALUE)
    {
        do {
            // Ignore "." and ".." entries.

            if (IsDotsDir(finddata.cFileName)) continue;

            if (!fliteral && !wildComp (subpattern, finddata.cFileName))
                continue;

            // Skip files if the pattern ended in a slash or if the original
            // pattern specified directories only.

            if ((m_dirsonly || fdirmatch) && !FEntryIsDir(finddata))
            {
                // Do nothing.
            }
            else if (fdescend)
            {
                wchar_t *pathend_new = AppendPath (pathend, finddata.cFileName);

                if (!pathend_new) continue;

                *pathend_new++ = c_slash;
                *pathend_new   = 0;

                MatchDir (pathend_new, pattern + ipatt + 1);
            }
            else
            {
                // Construct full relative entry path.

                if (AppendPath(pathend, finddata.cFileName))
                {
                    if (!m_callback (m_path, finddata, m_cbdata))
                        break;
                }
            }

        } while (FindNextFile (find_handle, &finddata));

        FindClose (find_handle);
    }

    delete subpattern;
    return;
}

//==============================================================================

void PathMatcher::HandleEllipsisSubpath (
    wchar_t       *pathend,
    const wchar_t *pattern,
    int            ipatt)
{
    //--------------------------------------------------------------------------
    // This function handles subdirectories that contain ellipses.
    //
    // The parameter 'pathend' points to one past the last character.
    // The 'pattern' parameter is a pointer to the beginning of the current
    // subdirectory of the full pattern. Finally, 'ipatt' is an integer offset
    // from pattern to beginning of the ellipsis.
    //--------------------------------------------------------------------------

    wchar_t *ellipsis_prefix = NULL;    // Pattern Filter for Prefixed Ellipses

    if ((ipatt == 0) && !pattern[ipatt+3])
    {
        // ...<end> - Just do a simple recursive fetch of the tree.

        m_ellpattern = NULL;
    }
    else
    {
        m_ellpattern = pattern;
        m_ellpath    = pathend;

        // If the ellipsis is prefixed with a pattern, then we want to save the
        // pattern for filtering of candidate directory entries by the FetchAll
        // routine.

        if (ipatt > 0)
        {
            ellipsis_prefix = new wchar_t [ipatt+2];

            if (!ellipsis_prefix ||
                FAILED(wcsncpy_s (ellipsis_prefix, ipatt+2, pattern, ipatt)))
            {
                return;
            }

            ellipsis_prefix[ipatt]   = L'*';
            ellipsis_prefix[ipatt+1] = 0;
        }
        else
        {
            ellipsis_prefix = NULL;
        }
    }

    FetchAll (pathend, ellipsis_prefix);
    return;
}

//==============================================================================

void PathMatcher::FetchAll (wchar_t* pathend, const wchar_t* ellipsis_prefix)
{
    //--------------------------------------------------------------------------
    // This procedure is called when an ellipsis is encountered, and recursively
    // fetches all tree entries and optionally matches against a pattern.
    //
    // 'pathend' is the end of the current path (one past last character)
    //
    // 'ellipsis_prefix' is the pattern that prefixes the ellipsis, followed by
    // an asterisk. It will be used to filter directory entries for subsequent
    // ellipsis pattern matching.
    //
    // This function silently returns on error.
    //--------------------------------------------------------------------------

    static const wchar_t c_slashstr[] = { c_slash, 0 };

    // Append slash if needed.

    if ((pathend > m_path) && !IsSlash(pathend[-1]))
    {   pathend = AppendPath (pathend, c_slashstr);
        if (!pathend) return;          // Bail out if the append failed.
    }

    // Bail out if we've run out of path length.

    if (PathSpaceLeft(pathend) < 1) return;

    pathend[0] = L'*';
    pathend[1] = 0;

    WIN32_FIND_DATA finddata;
    HANDLE find_handle = FindFirstFile (m_path, &finddata);

    if (find_handle == INVALID_HANDLE_VALUE)
        return;

    do {
        // Ignore "." and ".." entries.

        if (IsDotsDir(finddata.cFileName)) continue;

        // Skip file entries if we're only looking for directories.

        if (m_dirsonly && !FEntryIsDir(finddata))
            continue;

        // If there's an ellipsis prefix, then ensure first that we match
        // against it before descending further.

        if (ellipsis_prefix && !wildComp (ellipsis_prefix, finddata.cFileName))
            continue;

        wchar_t* pathend_new = AppendPath (pathend, finddata.cFileName);

        if (!pathend_new) break;

        if (!m_ellpattern || pathMatch(m_ellpattern, m_ellpath))
        {
            if (!m_callback (m_path, finddata, m_cbdata))
                return;
        }

        if (FEntryIsDir(finddata))
            FetchAll (pathend_new, NULL);

    } while (FindNextFile (find_handle, &finddata));

    FindClose (find_handle);
}



bool wildComp (const wchar_t *pattern, const wchar_t *string)
{
    //==============================================================================================
    // wildComp
    //     Compares a pattern against a string to determine if the two match. In the pattern string,
    //     the character '?' denotes any single character, and the character '*' denotes any number
    //     of characters. All other characters are interpreted literally, though they are compared
    //     without regard to case (for exampmle, 'a' matches 'A'). For case-sensitive matches, use
    //     wildcompc().
    //
    // Parameters
    //     pattern - The pattern to compare with the string
    //     string  - The string to test for matching
    //
    // Returns
    //     True if and only if the pattern matches the string. This function returns false if either
    //     the pattern or the string are null pointers.
    //==============================================================================================

    if (!pattern || !string) return false;

    // Scan through the single character matches.

    while (*pattern && *string)
    {
        if (*pattern == L'*')  // If we've hit an asterisk, then drop down to the section below.
            break;

        // Stop testing on mismatch.

        if ((*pattern != L'?') && (tolower(*pattern) != tolower(*string)))
            break;

        ++ pattern;    // On a successful match, increment the pattern and the string and continue.
        ++ string;
    }

    // Unless we stopped on an asterisk, we're done matching. The only valid way to match at this
    // point is if both the pattern and the string are exhausted.

    if (*pattern != L'*')
        return (*pattern == 0) && (*string == 0);

    // Advance past the asterisk. Handle pathological cases where there is more than one asterisk
    // in a row.

    while (*pattern == L'*')
        ++pattern;

    // If the asterisk is the last character of the pattern, then we match any remainder,
    // so return true.

    if (*pattern == 0)
        return true;

    // We're at an asterisk with other patterns following, so recursively eat away at the string
    // until we match or exhaust the string.

    while (true)
    {
        if (wildComp (pattern, string))
            return true;

        if (!*string++)
            return false;
    }
}



bool wildCompCaseSensitive (const wchar_t *pattern, const wchar_t *string)
{
    //==============================================================================================
    // wildCompCaseSensitive
    //     Compares a pattern against a string to determine if the two match. In the pattern string,
    //     the character '?' denotes any single character, and the character '*' denotes any number
    //     of characters. All other characters are interpreted literally, and must match case. For
    //     case-insensitive matching, use wildcomp.
    //
    // Parameters
    //     pattern - The pattern to compare with the string
    //     string  - The string to test for matching
    //
    // Returns
    //     True if and only if the pattern matches the string. This function returns false if either
    //     the pattern or the string are null pointers.
    //==============================================================================================

    if (!pattern || !string) return false;

    // Scan through the single character matches.

    while (*pattern && *string)
    {
        if (*pattern == L'*')  // If we've hit an asterisk, then drop down to the section below.
            break;

        // Stop testing on mismatch.

        if ((*pattern != L'?') && (*pattern != *string))
            break;

        ++ pattern;    // On a successful match, increment the pattern and the string and continue.
        ++ string;
    }

    // Unless we stopped on an asterisk, we're done matching. The only valid way to match at this
    // point is if both the pattern and the string are exhausted.

    if (*pattern != L'*')
        return (*pattern == 0) && (*string == 0);

    // Advance past the asterisk. Handle pathological cases where there is more than one asterisk
    // in a row.

    while (*pattern == L'*')
        ++pattern;

    // If the asterisk is the last character of the pattern, then we match any remainder,
    // so return true.

    if (*pattern == 0)
        return true;

    // We're at an asterisk with other patterns following, so recursively eat away at the string
    // until we match or exhaust the string.

    while (true)
    {
        if (wildCompCaseSensitive (pattern, string))
            return true;

        if (!*string++)
            return false;
    }
}



bool pathMatch (const wchar_t *pattern, const wchar_t *path)
{
    //==============================================================================================
    // pathmatch
    //     Compares a single path against a VMS-style wildcard specification. In the pattern string,
    //     the character '?' denotes any single character except '/', the character '*' denotes any
    //     number of characters except '/', and the sequence '...' denotes any number of characters
    //     including '/'. All other characters in the pattern are interpreted literally, though
    //     without regard to case. For example, 'a' matches 'A'.
    //
    //     Note that this routine also interprets a backslash ('\') as a euphemism for a forward
    //     slash.
    //
    //     Also note:
    //
    //         - Multiple slashes compare as a single slash. That is, "/////" compares the same
    //           as "/".
    //
    //         - ".../" and "*\" (without regard to slash type) both match the empty string, so
    //           ".../foo" and "*\foo" both match "foo". To exclude this rule, use a question mark
    //           like so: "...?/foo".
    //
    // Parameters
    //     pattern - The pattern to compare with the path.
    //     path    - The path to test for matching
    //
    // Returns
    //     True if and only if the pattern matches the given path. This function returns false if
    //     the pattern is null or empty, or if the path is null.
    //==============================================================================================

    if (!pattern || !path) return false;

    // Scan through the pattern and path until we hit an asterisk or an ellipsis. Handle the special
    // cases of "/.../" and "/*/", tested against null subdirectories (where both are also
    // equivalent to "/").

    while (*pattern && *path)
    {
        if (IsSlash(path[0]))             // Consume repeated slashes on path.
        {   while (IsSlash(path[1]))
                ++ path;
        }

        if (IsSlash(pattern[0]))
        {
            if (!IsSlash(path[0]))
                return false;

            while (IsSlash(pattern[1]))  // Consume repeated slashes on pattern.
                ++ pattern;
        }
        else if (IsMultiWildStr (pattern))
        {   // If we've hit a multi-character wildcard character, then drop to section below.
            break;
        }

        // Test for a single character match. In order to support case-sensitive path matching,
        // you'd only need to change the tolower comparison below.

        if (*pattern != L'?')
        {   if (tolower(*pattern) != tolower(*path))
                return false;
        }
        else if (IsSlash(*path))         // '?' matches all but slash.
        {   return false;
        }

        ++ pattern;    // On a successful match, increment the pattern and the path and continue.
        ++ path;
    }

    // Unless we stopped on a multi-character wildcard, we're done matching. The only valid way to
    // match at this point is if both the pattern and the path are exhausted.

    if (!IsMultiWildStr(pattern))
        return (*pattern == 0) && (*path == 0);

    // Advance past the multi-character wildcard(s). A sequence of asterisks is equivalent to a
    // single asterisk, and a sequence of ellipses and asterisks is equivalent to a single
    // ellipsis. We handle this here because many asterisks and ellipses in a row would yield
    // exponential (and pathological) runtimes.

    bool fEllipsis = false;

    while (IsMultiWildStr (pattern))
    {
        if (pattern[0] == L'*')
            pattern += 1;
        else
        {   pattern += 3;
            fEllipsis = true;
        }
    }

    // If the pattern ends in an ellipsis, then we trivially match any remainder of the path, so
    // return true, otherwise perform match testing.

    if (fEllipsis && (*pattern == 0))
        return true;

    // A multi-wild pattern (* or ...) followed by any number of slashes can match the empty string,
    // so we test for that here. Thus, ".../foo" will match against "foo".

    if (IsSlash(*pattern))
    {
        // Search forward past any number of trailing slashes.

        const wchar_t *ptr = pattern + 1;

        while (IsSlash(*ptr)) ++ptr;

        // Match the remainder of the pattern against the remainder of the path.

        if (pathMatch(ptr,path))
            return true;
    }

    if (fEllipsis)
    {
        // If we have an ellipsis, then recursively nibble away at the path to see if we can yield
        // a match, until we either match or exhaust the path.

        for (;; ++path)
        {
            if (pathMatch(pattern, path)) return true;
            if (*path == 0) return false;
        }
    }
    else
    {
        // If we have an asterisk, then recursively nibble away at the path until we encounter a
        // slash or exhaust the path.

        for (; *path && !IsSlash(*path);  ++path)
        {
            if (pathMatch(pattern, path)) return true;
        }

        // Test the remainder of the pattern and path.

        return pathMatch(pattern, path);
    }
}

}; // Namespace PathMatch