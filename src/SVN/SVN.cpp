// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2009 - TortoiseSVN

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
#pragma warning(push)
#include "svn.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "client.h"
#include "svn_compat.h"
#pragma warning(pop)

#include "TortoiseProc.h"
#include "ProgressDlg.h"
#include "UnicodeUtils.h"
#include "DirFileEnum.h"
#include "TSVNPath.h"
#include "ShellUpdater.h"
#include "Registry.h"
#include "SVNHelpers.h"
#include "SVNStatus.h"
#include "SVNInfo.h"
#include "AppUtils.h"
#include "PathUtils.h"
#include "StringUtils.h"
#include "TempFile.h"
#include "SVNAdminDir.h"
#include "SVNError.h"
#include "SVNLogQuery.h"
#include "CacheLogQuery.h"
#include "RepositoryInfo.h"
#include "MessageBox.h"
#include "LogCacheSettings.h"
#include "..\version.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


LCID SVN::s_locale = MAKELCID((DWORD)CRegStdDWORD(_T("Software\\TortoiseSVN\\LanguageID"), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)), SORT_DEFAULT);
bool SVN::s_useSystemLocale = !!(DWORD)CRegStdDWORD(_T("Software\\TortoiseSVN\\UseSystemLocaleForDates"), TRUE);

/* Number of micro-seconds between the beginning of the Windows epoch
* (Jan. 1, 1601) and the Unix epoch (Jan. 1, 1970) 
*/
#define APR_DELTA_EPOCH_IN_USEC   APR_TIME_C(11644473600000000);

__inline void AprTimeToFileTime(LPFILETIME pft, apr_time_t t)
{
	LONGLONG ll;
	t += APR_DELTA_EPOCH_IN_USEC;
	ll = t * 10;
	pft->dwLowDateTime = (DWORD)ll;
	pft->dwHighDateTime = (DWORD) (ll >> 32);
	return;
}


SVN::SVN(void) : m_progressWnd(0)
	, m_pProgressDlg(NULL)
	, m_bShowProgressBar(false)
	, progress_total(0)
	, progress_lastprogress(0)
	, progress_lasttotal(0)
	, logCachePool()
{
	parentpool = svn_pool_create(NULL);
	svn_error_clear(svn_client_create_context(&m_pctx, parentpool));

	Err = svn_config_ensure(NULL, parentpool);
	pool = svn_pool_create (parentpool);
	// set up the configuration
	if (Err == 0)
		Err = svn_config_get_config (&(m_pctx->config), g_pConfigDir, parentpool);

	if (Err != 0)
	{
		::MessageBox(NULL, this->GetLastErrorMessage(), _T("TortoiseSVN"), MB_ICONERROR);
		svn_pool_destroy (pool);
		svn_pool_destroy (parentpool);
		exit(-1);
	}

	// set up authentication
	m_prompt.Init(parentpool, m_pctx);

	m_pctx->log_msg_func3 = svn_cl__get_log_message;
	m_pctx->log_msg_baton3 = logMessage("");
	m_pctx->notify_func2 = notify;
	m_pctx->notify_baton2 = this;
	m_pctx->notify_func = NULL;
	m_pctx->notify_baton = NULL;
	m_pctx->conflict_func = conflict_resolver;
	m_pctx->conflict_baton = this;
	m_pctx->cancel_func = cancel;
	m_pctx->cancel_baton = this;
	m_pctx->progress_func = progress_func;
	m_pctx->progress_baton = this;
	char namestring[MAX_PATH] = {0};
	sprintf_s(namestring, MAX_PATH, "TortoiseSVN-%d.%d.%d.%d", TSVN_VERMAJOR, TSVN_VERMINOR, TSVN_VERMICRO, TSVN_VERBUILD);
	m_pctx->client_name = apr_pstrdup(pool, namestring);


	//set up the SVN_SSH param
	CString tsvn_ssh = CRegString(_T("Software\\TortoiseSVN\\SSH"));
	if (tsvn_ssh.IsEmpty())
	{
		// check whether the ssh client is already set in the Subversion config
		svn_config_t * cfg = (svn_config_t *)apr_hash_get (m_pctx->config, SVN_CONFIG_CATEGORY_CONFIG,
			APR_HASH_KEY_STRING);
		const char * sshValue = NULL;
		svn_config_get(cfg, &sshValue, SVN_CONFIG_SECTION_TUNNELS, "ssh", "");
		if ((sshValue == NULL)||(sshValue[0] == 0))
			tsvn_ssh = _T("\"") + CPathUtils::GetAppDirectory() + _T("TortoisePlink.exe") + _T("\"");
	}
	tsvn_ssh.Replace('\\', '/');
	if (!tsvn_ssh.IsEmpty())
	{
		svn_config_t * cfg = (svn_config_t *)apr_hash_get (m_pctx->config, SVN_CONFIG_CATEGORY_CONFIG,
			APR_HASH_KEY_STRING);
		svn_config_set(cfg, SVN_CONFIG_SECTION_TUNNELS, "ssh", CUnicodeUtils::GetUTF8(tsvn_ssh));
	}
}

SVN::~SVN(void)
{
	svn_error_clear(Err);
	svn_pool_destroy (parentpool);

    if (logCachePool.get() != NULL)
	    logCachePool->Flush();
}

CString SVN::CheckConfigFile()
{
	svn_client_ctx_t *			ctx;
	SVNPool						pool;
	svn_error_t *				err = NULL;

	svn_client_create_context(&ctx, pool);

	err = svn_config_ensure(NULL, pool);
	// set up the configuration
	if (err == 0)
		err = svn_config_get_config (&ctx->config, g_pConfigDir, pool);
	CString msg;
	CString temp;
	if (err != NULL)
	{
		svn_error_t * ErrPtr = err;
		msg = CUnicodeUtils::GetUnicode(ErrPtr->message);
		while (ErrPtr->child)
		{
			ErrPtr = ErrPtr->child;
			msg += _T("\n");
			msg += CUnicodeUtils::GetUnicode(ErrPtr->message);
		}
		if (!temp.IsEmpty())
		{
			msg += _T("\n") + temp;
		}
		svn_error_clear(err);
        err = NULL;
	}
	return msg;
}

#pragma warning(push)
#pragma warning(disable: 4100)	// unreferenced formal parameter

BOOL SVN::Cancel() {return FALSE;}

BOOL SVN::Notify(const CTSVNPath& path, const CTSVNPath url, svn_wc_notify_action_t action, 
				svn_node_kind_t kind, const CString& mime_type, 
				svn_wc_notify_state_t content_state, 
				svn_wc_notify_state_t prop_state, svn_revnum_t rev,
				const svn_lock_t * lock, svn_wc_notify_lock_state_t lock_state,
				const CString& changelistname,
				const CString& propertyName,
				svn_merge_range_t * range,
				svn_error_t * err, apr_pool_t * pool) {return TRUE;};
BOOL SVN::Log(svn_revnum_t rev, const CString& author, const CString& date, const CString& message, LogChangedPathArray * cpaths, apr_time_t time, int filechanges, BOOL copies, DWORD actions, BOOL haschildren) {return TRUE;};
BOOL SVN::BlameCallback(LONG linenumber, svn_revnum_t revision, const CString& author, const CString& date, svn_revnum_t merged_revision, const CString& merged_author, const CString& merged_date, const CString& merged_path, const CStringA& line) {return TRUE;}
svn_error_t* SVN::DiffSummarizeCallback(const CTSVNPath& path, svn_client_diff_summarize_kind_t kind, bool propchanged, svn_node_kind_t node) {return SVN_NO_ERROR;}
BOOL SVN::ReportList(const CString& path, svn_node_kind_t kind, 
					 svn_filesize_t size, bool has_props, 
					 svn_revnum_t created_rev, apr_time_t time, 
					 const CString& author, const CString& locktoken, 
					 const CString& lockowner, const CString& lockcomment, 
					 bool is_dav_comment, apr_time_t lock_creationdate, 
					 apr_time_t lock_expirationdate, const CString& absolutepath) {return TRUE;}
svn_wc_conflict_choice_t SVN::ConflictResolveCallback(const svn_wc_conflict_description_t *description, CString& mergedfile) {return svn_wc_conflict_choose_postpone;}

#pragma warning(pop)

struct log_msg_baton3
{
  const char *message;  /* the message. */
  const char *message_encoding; /* the locale/encoding of the message. */
  const char *base_dir; /* the base directory for an external edit. UTF-8! */
  const char *tmpfile_left; /* the tmpfile left by an external edit. UTF-8! */
  apr_pool_t *pool; /* a pool. */
};

CString SVN::GetLastErrorMessage(int wrap /* = 80 */)
{
	CString msg = GetErrorString(Err, wrap);
	if (!PostCommitErr.IsEmpty())
	{
		msg += _T("\n") + CStringUtils::LinesWrap(PostCommitErr, wrap);
	}
	return msg;
}

CString SVN::GetErrorString(svn_error_t * Err, int wrap /* = 80 */)
{
	CString msg;
	CString temp;
	char errbuf[256];

	if (Err != NULL)
	{
		svn_error_t * ErrPtr = Err;
		if (ErrPtr->message)
			msg = CUnicodeUtils::GetUnicode(ErrPtr->message);
		else
		{
			/* Is this a Subversion-specific error code? */
			if ((ErrPtr->apr_err > APR_OS_START_USEERR)
				&& (ErrPtr->apr_err <= APR_OS_START_CANONERR))
				msg = svn_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf));
			/* Otherwise, this must be an APR error code. */
			else
			{
				svn_error_t *temp_err = NULL;
				const char * err_string = NULL;
				temp_err = svn_utf_cstring_to_utf8(&err_string, apr_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf)-1), ErrPtr->pool);
				if (temp_err)
				{
					svn_error_clear (temp_err);
					msg = _T("Can't recode error string from APR");
				}
				else
				{
					msg = CUnicodeUtils::GetUnicode(err_string);
				}
			}
		}
		msg = CStringUtils::LinesWrap(msg, wrap);
		while (ErrPtr->child)
		{
			ErrPtr = ErrPtr->child;
			msg += _T("\n");
			if (ErrPtr->message)
				temp = CUnicodeUtils::GetUnicode(ErrPtr->message);
			else
			{
				/* Is this a Subversion-specific error code? */
				if ((ErrPtr->apr_err > APR_OS_START_USEERR)
					&& (ErrPtr->apr_err <= APR_OS_START_CANONERR))
					temp = svn_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf));
				/* Otherwise, this must be an APR error code. */
				else
				{
					svn_error_t *temp_err = NULL;
					const char * err_string = NULL;
					temp_err = svn_utf_cstring_to_utf8(&err_string, apr_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf)-1), ErrPtr->pool);
					if (temp_err)
					{
						svn_error_clear (temp_err);
						temp = _T("Can't recode error string from APR");
					}
					else
					{
						temp = CUnicodeUtils::GetUnicode(err_string);
					}
				}
			}
			temp = CStringUtils::LinesWrap(temp, wrap);
			msg += temp;
		}
		temp.Empty();
		// add some hint text for some of the error messages
		switch (Err->apr_err)
		{
		case SVN_ERR_BAD_FILENAME:
		case SVN_ERR_BAD_URL:
			// please check the path or URL you've entered.
			temp.LoadString(IDS_SVNERR_CHECKPATHORURL);
			break;
		case SVN_ERR_WC_LOCKED:
		case SVN_ERR_WC_NOT_LOCKED:
			// do a "cleanup"
			temp.LoadString(IDS_SVNERR_RUNCLEANUP);
			break;
		case SVN_ERR_WC_NOT_UP_TO_DATE:
		case SVN_ERR_FS_TXN_OUT_OF_DATE:
			// do an update first
			temp.LoadString(IDS_SVNERR_UPDATEFIRST);
			break;
		case SVN_ERR_WC_CORRUPT:
		case SVN_ERR_WC_CORRUPT_TEXT_BASE:
			// do a "cleanup". If that doesn't work you need to do a fresh checkout.
			temp.LoadString(IDS_SVNERR_CLEANUPORFRESHCHECKOUT);
			break;
		default:
			break;
		}
		if ((Err->apr_err == SVN_ERR_FS_PATH_NOT_LOCKED)||
			(Err->apr_err == SVN_ERR_FS_NO_SUCH_LOCK)||
			(Err->apr_err == SVN_ERR_RA_NOT_LOCKED))
		{
			// the lock has already been broken from another working copy
			temp.LoadString(IDS_SVNERR_UNLOCKFAILEDNOLOCK);
		}
		else if (SVN_ERR_IS_UNLOCK_ERROR(Err))
		{
			// if you want to break the lock, use the "check for modifications" dialog
			temp.LoadString(IDS_SVNERR_UNLOCKFAILED);
		}
		if (!temp.IsEmpty())
		{
			msg += _T("\n") + temp;
		}
		return msg;
	}
	return _T("");
}

BOOL SVN::Checkout(const CTSVNPath& moduleName, const CTSVNPath& destPath, const SVNRev& pegrev, 
				   const SVNRev& revision, svn_depth_t depth, BOOL bIgnoreExternals, 
				   BOOL bAllow_unver_obstructions)
{
	SVNPool subpool(pool);
	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_checkout3 (NULL,			// we don't need the resulting revision
								moduleName.GetSVNApiPath(subpool),
								destPath.GetSVNApiPath(subpool),
								pegrev,
								revision,
								depth,
								bIgnoreExternals,
								bAllow_unver_obstructions,
								m_pctx,
								subpool );

	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::Remove(const CTSVNPathList& pathlist, BOOL force, BOOL keeplocal, const CString& message, const RevPropHash revProps)
{
	// svn_client_delete needs to run on a sub-pool, so that after it's run, the pool
	// cleanups get run.  For example, after a failure do to an unforced delete on 
	// a modified file, if you don't do a cleanup, the WC stays locked
	SVNPool subPool(pool);
	svn_error_clear(Err);
	Err = NULL;
	svn_commit_info_t *commit_info = svn_create_commit_info(subPool);
	m_pctx->log_msg_baton3 = logMessage(CUnicodeUtils::GetUTF8(message));

	apr_hash_t * revPropHash = MakeRevPropHash(revProps, subPool);

	Err = svn_client_delete3 (&commit_info, pathlist.MakePathArray(subPool), 
							  force,
							  keeplocal,
							  revPropHash,
							  m_pctx,
							  subPool);
	if(Err != NULL)
	{
		return FALSE;
	}

	PostCommitErr.Empty();
	if (commit_info)
	{
		if (SVN_IS_VALID_REVNUM(commit_info->revision))
		{
			for (int i=0; i<pathlist.GetCount(); ++i)
				Notify(pathlist[i], CTSVNPath(), svn_wc_notify_update_completed, svn_node_none, _T(""), 
						svn_wc_notify_state_unknown, svn_wc_notify_state_unknown, 
						commit_info->revision, NULL, svn_wc_notify_lock_state_unchanged, 
						_T(""), _T(""), NULL, NULL, pool);
		}
		if (commit_info->post_commit_err)
		{
			PostCommitErr = CUnicodeUtils::GetUnicode(commit_info->post_commit_err);
		}
	}

	for(int nPath = 0; nPath < pathlist.GetCount(); nPath++)
	{
		if ((!keeplocal)&&(!pathlist[nPath].IsDirectory()))
		{
			SHChangeNotify(SHCNE_DELETE, SHCNF_PATH | SHCNF_FLUSHNOWAIT, pathlist[nPath].GetWinPath(), NULL);
		}
		else
		{
			SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSHNOWAIT, pathlist[nPath].GetWinPath(), NULL);
		}
	}

	return TRUE;
}

BOOL SVN::Revert(const CTSVNPathList& pathlist, const CStringArray& changelists, BOOL recurse)
{
	TRACE("Reverting list of %d files\n", pathlist.GetCount());
	SVNPool subpool(pool);
	apr_array_header_t * clists = MakeChangeListArray(changelists, subpool);

	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_revert2(pathlist.MakePathArray(subpool), 
		recurse ? svn_depth_infinity : svn_depth_empty, 
		clists,
		m_pctx, 
		subpool);

	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}


BOOL SVN::Add(const CTSVNPathList& pathList, ProjectProperties * props, svn_depth_t depth, BOOL force, BOOL no_ignore, BOOL addparents)
{
	// the add command should use the mime-type file
	const char *mimetypes_file;
	svn_error_clear(Err);
    Err = NULL;

	svn_config_t * opt = (svn_config_t *)apr_hash_get (m_pctx->config, SVN_CONFIG_CATEGORY_CONFIG,
		APR_HASH_KEY_STRING);
	svn_config_get(opt, &mimetypes_file,
		SVN_CONFIG_SECTION_MISCELLANY,
		SVN_CONFIG_OPTION_MIMETYPES_FILE, FALSE);
	if (mimetypes_file && *mimetypes_file)
	{
		Err = svn_io_parse_mimetypes_file(&(m_pctx->mimetypes_map),
			mimetypes_file, pool);
		if (Err)
			return FALSE;
	}
	if (props)
		props->InsertAutoProps(opt);

	for(int nItem = 0; nItem < pathList.GetCount(); nItem++)
	{
		TRACE(_T("add file %s\n"), pathList[nItem].GetWinPath());
		if (Cancel())
		{
			Err = svn_error_create(NULL, NULL, CUnicodeUtils::GetUTF8(CString(MAKEINTRESOURCE(IDS_SVN_USERCANCELLED))));
			return FALSE;
		}
		SVNPool subpool(pool);
		Err = svn_client_add4 (pathList[nItem].GetSVNApiPath(subpool), depth, force, no_ignore, addparents, m_pctx, subpool);
		if(Err != NULL)
		{
			return FALSE;
		}
		if ((props)&&(pathList[nItem].IsDirectory()))
		{
			// try adding the project properties
			props->AddAutoProps(pathList[nItem]);
		}
	}

	return TRUE;
}

BOOL SVN::AddToChangeList(const CTSVNPathList& pathList, const CString& changelist, svn_depth_t depth, const CStringArray& changelists)
{
	SVNPool subpool(pool);
	svn_error_clear(Err);
	Err = NULL;

	apr_array_header_t *clists = MakeChangeListArray(changelists, subpool);

	Err = svn_client_add_to_changelist(pathList.MakePathArray(subpool), 
		changelist.IsEmpty() ? NULL : (LPCSTR)CUnicodeUtils::GetUTF8(changelist), 
		depth,
		clists,
		m_pctx, 
		subpool);
	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::RemoveFromChangeList(const CTSVNPathList& pathList, const CStringArray& changelists, svn_depth_t depth)
{
	SVNPool subpool(pool);
	svn_error_clear(Err);
	Err = NULL;
	apr_array_header_t * clists = MakeChangeListArray(changelists, subpool);

	Err = svn_client_remove_from_changelists(pathList.MakePathArray(subpool), 
		depth,
		clists, 
		m_pctx, 
		subpool);
	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::Update(const CTSVNPathList& pathList, const SVNRev& revision, svn_depth_t depth, BOOL depthIsSticky, BOOL ignoreexternals, BOOL bAllow_unver_obstructions)
{
	SVNPool(localpool);
	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_update3(NULL,
							pathList.MakePathArray(pool),
							revision,
							depth,
							depthIsSticky,
							ignoreexternals,
							bAllow_unver_obstructions,
							m_pctx,
							localpool);

	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

svn_revnum_t SVN::Commit(const CTSVNPathList& pathlist, const CString& message, 
						 const CStringArray& changelists, BOOL keepchangelist, svn_depth_t depth, BOOL keep_locks, const RevPropHash revProps)
{
	SVNPool localpool(pool);

	svn_error_clear(Err);
	Err = NULL;
	svn_commit_info_t *commit_info = svn_create_commit_info(localpool);

	apr_array_header_t *clists = MakeChangeListArray(changelists, localpool);

	apr_hash_t * revprop_table = MakeRevPropHash(revProps, localpool);

	m_pctx->log_msg_baton3 = logMessage(CUnicodeUtils::GetUTF8(message));
	Err = svn_client_commit4 (&commit_info, 
							pathlist.MakePathArray(pool), 
							depth,
							keep_locks,
							keepchangelist,
							clists,
							revprop_table,
							m_pctx,
							localpool);
	m_pctx->log_msg_baton3 = logMessage("");
	if(Err != NULL)
	{
		return 0;
	}

	svn_revnum_t finrev = -1;
	PostCommitErr.Empty();
	if (commit_info)
	{
		if (SVN_IS_VALID_REVNUM(commit_info->revision))
		{
			Notify(CTSVNPath(), CTSVNPath(), svn_wc_notify_update_completed, svn_node_none, _T(""), 
					svn_wc_notify_state_unknown, svn_wc_notify_state_unknown, 
					commit_info->revision, NULL, svn_wc_notify_lock_state_unchanged, 
					_T(""), _T(""), NULL, NULL, localpool);
			finrev = commit_info->revision;
		}
		if (commit_info->post_commit_err)
		{
			PostCommitErr = CUnicodeUtils::GetUnicode(commit_info->post_commit_err);
		}
	}

	return finrev;
}

BOOL SVN::Copy(const CTSVNPathList& srcPathList, const CTSVNPath& destPath, 
			   const SVNRev& revision, const SVNRev& pegrev, const CString& logmsg, bool copy_as_child, 
			   bool make_parents, bool ignoreExternals, const RevPropHash revProps)
{
	SVNPool subpool(pool);

	svn_error_clear(Err);
	Err = NULL;
	svn_commit_info_t *commit_info = svn_create_commit_info(subpool);

	m_pctx->log_msg_baton3 = logMessage(CUnicodeUtils::GetUTF8(logmsg));
	apr_hash_t * revPropHash = MakeRevPropHash(revProps, subpool);

	Err = svn_client_copy5 (&commit_info,
							MakeCopyArray(srcPathList, revision, pegrev),
							destPath.GetSVNApiPath(subpool),
							copy_as_child,
							make_parents,
							ignoreExternals,
							revPropHash,
							m_pctx,
							subpool);
	if(Err != NULL)
	{
		return FALSE;
	}

	PostCommitErr.Empty();
	if (commit_info)
	{
		if (SVN_IS_VALID_REVNUM(commit_info->revision))
		{
			Notify(destPath, CTSVNPath(), svn_wc_notify_update_completed, svn_node_none, _T(""), 
					svn_wc_notify_state_unknown, svn_wc_notify_state_unknown, 
					commit_info->revision, NULL, svn_wc_notify_lock_state_unchanged, 
					_T(""), _T(""), NULL, NULL, pool);
		}
		if (commit_info->post_commit_err)
		{
			PostCommitErr = CUnicodeUtils::GetUnicode(commit_info->post_commit_err);
		}
	}

	return TRUE;
}

BOOL SVN::Move(const CTSVNPathList& srcPathList, const CTSVNPath& destPath, 
			   BOOL force, const CString& message /* = _T("")*/, 
			   bool move_as_child /* = false*/, bool make_parents /* = false */,
			   const RevPropHash revProps /* = RevPropHash() */ )
{
	SVNPool subpool(pool);

	svn_error_clear(Err);
	Err = NULL;
	svn_commit_info_t *commit_info = svn_create_commit_info(subpool);
	m_pctx->log_msg_baton3 = logMessage(CUnicodeUtils::GetUTF8(message));
	apr_hash_t * revPropHash = MakeRevPropHash(revProps, subpool);
	Err = svn_client_move5 (&commit_info,
							srcPathList.MakePathArray(subpool),
							destPath.GetSVNApiPath(subpool),
							force,
							move_as_child,
							make_parents,
							revPropHash,
							m_pctx,
							subpool);
	if(Err != NULL)
	{
		return FALSE;
	}

	PostCommitErr.Empty();
	if (commit_info)
	{
		if (SVN_IS_VALID_REVNUM(commit_info->revision))
		{
			Notify(destPath, CTSVNPath(), svn_wc_notify_update_completed, svn_node_none, _T(""), 
					svn_wc_notify_state_unknown, svn_wc_notify_state_unknown, 
					commit_info->revision, NULL, svn_wc_notify_lock_state_unchanged, 
					_T(""), _T(""), NULL, NULL, pool);
		}
		if (commit_info->post_commit_err)
		{
			PostCommitErr = CUnicodeUtils::GetUnicode(commit_info->post_commit_err);
		}
	}

	return TRUE;
}

BOOL SVN::MakeDir(const CTSVNPathList& pathlist, const CString& message, bool makeParents, const RevPropHash revProps)
{
	svn_error_clear(Err);
	Err = NULL;
	svn_commit_info_t *commit_info = svn_create_commit_info(pool);
	m_pctx->log_msg_baton3 = logMessage(CUnicodeUtils::GetUTF8(message));
	apr_hash_t * revPropHash = MakeRevPropHash(revProps, pool);
	Err = svn_client_mkdir3 (&commit_info,
							 pathlist.MakePathArray(pool),
							 makeParents,
							 revPropHash,
							 m_pctx,
							 pool);
	if(Err != NULL)
	{
		return FALSE;
	}

	PostCommitErr.Empty();
	if (commit_info)
	{
		if (SVN_IS_VALID_REVNUM(commit_info->revision))
		{
			for (int i=0; i<pathlist.GetCount(); ++i)
				Notify(pathlist[i], CTSVNPath(), svn_wc_notify_update_completed, svn_node_none, _T(""), 
						svn_wc_notify_state_unknown, svn_wc_notify_state_unknown, 
						commit_info->revision, NULL, svn_wc_notify_lock_state_unchanged, 
						_T(""), _T(""), NULL, NULL, pool);
		}
		if (commit_info->post_commit_err)
		{
			PostCommitErr = CUnicodeUtils::GetUnicode(commit_info->post_commit_err);
		}
	}

	return TRUE;
}

BOOL SVN::CleanUp(const CTSVNPath& path)
{
	svn_error_clear(Err);
	Err = NULL;
	SVNPool subpool(pool);
	Err = svn_client_cleanup (path.GetSVNApiPath(subpool), m_pctx, subpool);

	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::Resolve(const CTSVNPath& path, svn_wc_conflict_choice_t result, BOOL recurse)
{
	SVNPool subpool(pool);
	svn_error_clear(Err);
    Err = NULL;

	// before marking a file as resolved, we move the conflicted parts
	// to the trash bin: just in case the user later wants to get those
	// files back anyway
	svn_wc_status2_t * s;
	SVNStatus status;
	CTSVNPath retPath;
	if ((s = status.GetFirstFileStatus(path, retPath, false, svn_depth_empty, true, true))!=0)
	{
		if (s && !s->tree_conflict && s->entry)
		{
			CTSVNPathList conflictedEntries;
			if ((s->entry->conflict_new)&&(result != svn_wc_conflict_choose_theirs_full))
			{
				CTSVNPath conflictpath = path.GetContainingDirectory();
				conflictpath.AppendPathString(CUnicodeUtils::GetUnicode(s->entry->conflict_new));
				conflictedEntries.AddPath(conflictpath);
			}
			if ((s->entry->conflict_old)&&(result != svn_wc_conflict_choose_merged))
			{
				CTSVNPath conflictpath = path.GetContainingDirectory();
				conflictpath.AppendPathString(CUnicodeUtils::GetUnicode(s->entry->conflict_old));
				conflictedEntries.AddPath(conflictpath);
			}
			if ((s->entry->conflict_wrk)&&(result != svn_wc_conflict_choose_mine_full))
			{
				CTSVNPath conflictpath = path.GetContainingDirectory();
				conflictpath.AppendPathString(CUnicodeUtils::GetUnicode(s->entry->conflict_wrk));
				conflictedEntries.AddPath(conflictpath);
			}
			conflictedEntries.DeleteAllFiles(true);
		}
	}
	Err = svn_client_resolve(path.GetSVNApiPath(subpool),
							 recurse ? svn_depth_infinity : svn_depth_empty,
							 result,
							 m_pctx,
							 subpool);
	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::Export(const CTSVNPath& srcPath, const CTSVNPath& destPath, const SVNRev& pegrev, const SVNRev& revision, 
				 BOOL force, BOOL bIgnoreExternals, svn_depth_t depth, HWND hWnd, 
				 BOOL extended, const CString& eol)
{
	svn_error_clear(Err);
    Err = NULL;

	if (revision.IsWorking())
	{
		if (g_SVNAdminDir.IsAdminDirPath(srcPath.GetWinPath()))
			return FALSE;
		// files are special!
		if (!srcPath.IsDirectory())
		{
			CopyFile(srcPath.GetWinPath(), destPath.GetWinPath(), FALSE);
			SetFileAttributes(destPath.GetWinPath(), FILE_ATTRIBUTE_NORMAL);
			return TRUE;
		}
		// our own "export" function with a callback and the ability to export
		// unversioned items too. With our own function, we can show the progress
		// of the export with a progress bar - that's not possible with the
		// Subversion API.
		// BUG?: If a folder is marked as deleted, we export that folder too!
		CProgressDlg progress;
		progress.SetTitle(IDS_PROC_EXPORT_3);
		progress.SetAnimation(IDR_MOVEANI);
		progress.FormatNonPathLine(1, IDS_SVNPROGRESS_EXPORTINGWAIT);
		progress.SetTime(true);
		progress.ShowModeless(hWnd);
		std::map<CString, CString> copyMap;
		if (extended)
		{

			CString srcfile;
			CDirFileEnum lister(srcPath.GetWinPathString());
			copyMap[srcPath.GetWinPath()] = destPath.GetWinPath();
			while (lister.NextFile(srcfile, NULL))
			{
				if (g_SVNAdminDir.IsAdminDirPath(srcfile))
					continue;
				CString destination = destPath.GetWinPathString() + _T("\\") + srcfile.Mid(srcPath.GetWinPathString().GetLength());
				bool bNet = (destination.Left(2).Compare(_T("\\\\")) == 0);
				destination.Replace(_T("\\\\"), _T("\\"));
				if (bNet)
					destination = _T("\\") + destination;
				copyMap[srcfile] = destination;
			}
		}
		else
		{
			CTSVNPath statusPath;
			svn_wc_status2_t * s;
			SVNStatus status;
			if ((s = status.GetFirstFileStatus(srcPath, statusPath, false, svn_depth_infinity, true, !!bIgnoreExternals))!=0)
			{
				if (SVNStatus::GetMoreImportant(s->text_status, svn_wc_status_unversioned)!=svn_wc_status_unversioned)
				{
					CString src = statusPath.GetWinPathString();
					CString destination = destPath.GetWinPathString() + _T("\\") + src.Mid(srcPath.GetWinPathString().GetLength());
					copyMap[src] = destination;
				}
				while ((s = status.GetNextFileStatus(statusPath))!=0)
				{
					if ((s->text_status == svn_wc_status_unversioned)||
						(s->text_status == svn_wc_status_ignored)||
						(s->text_status == svn_wc_status_none)||
						(s->text_status == svn_wc_status_missing)||
						(s->text_status == svn_wc_status_deleted))
						continue;
					
					CString src = statusPath.GetWinPathString();
					CString destination = destPath.GetWinPathString() + _T("\\") + src.Mid(srcPath.GetWinPathString().GetLength());
					copyMap[src] = destination;
				}
			}
			else
			{
				Err = svn_error_create(status.m_err->apr_err, status.m_err, NULL);
				return FALSE;
			}
		} // else from if (extended)
		size_t count = 0;
		for (std::map<CString, CString>::iterator it = copyMap.begin(); (it != copyMap.end()) && (!progress.HasUserCancelled()); ++it)
		{
			progress.FormatPathLine(1, IDS_SVNPROGRESS_EXPORTING, (LPCTSTR)it->first);
			progress.FormatPathLine(2, IDS_SVNPROGRESS_EXPORTINGTO, (LPCTSTR)it->second);
			progress.SetProgress(count, copyMap.size());
			count++;
			if (PathIsDirectory((LPCTSTR)it->first))
				CPathUtils::MakeSureDirectoryPathExists((LPCTSTR)it->second);
			else
			{
				if (!CopyFile((LPCTSTR)it->first, (LPCTSTR)it->second, !force))
				{
					DWORD lastError = GetLastError();
					if ((lastError == ERROR_ALREADY_EXISTS)||(lastError == ERROR_FILE_EXISTS))
					{
						lastError = 0;
						CString sQuestion, yes, no, yestoall;
						sQuestion.Format(IDS_PROC_OVERWRITE_CONFIRM, (LPCTSTR)it->second);
						yes.LoadString(IDS_MSGBOX_YES);
						no.LoadString(IDS_MSGBOX_NO);
						yestoall.LoadString(IDS_PROC_YESTOALL);
						UINT ret = CMessageBox::Show(hWnd, sQuestion, _T("TortoiseSVN"), 2, IDI_QUESTION, yes, no, yestoall);
						if (ret == 3)
							force = true;
						if ((ret == 1)||(ret == 3))
						{
							if (!CopyFile((LPCTSTR)it->first, (LPCTSTR)it->second, FALSE))
							{
								lastError = GetLastError();
							}
							SetFileAttributes((LPCTSTR)it->second, FILE_ATTRIBUTE_NORMAL);
						}
					}
					if (lastError)
					{
						LPVOID lpMsgBuf;
						if (!FormatMessage( 
							FORMAT_MESSAGE_ALLOCATE_BUFFER | 
							FORMAT_MESSAGE_FROM_SYSTEM | 
							FORMAT_MESSAGE_IGNORE_INSERTS,
							NULL,
							lastError,
							MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
							(LPTSTR) &lpMsgBuf,
							0,
							NULL ))
						{
							return FALSE;
						}
						Err = svn_error_create(NULL, NULL, CUnicodeUtils::GetUTF8(CString((LPCTSTR)lpMsgBuf)));
						LocalFree(lpMsgBuf);
						return FALSE;
					}
				}
				SetFileAttributes((LPCTSTR)it->second, FILE_ATTRIBUTE_NORMAL);
			}
		}
		if (progress.HasUserCancelled())
		{
			progress.Stop();
			Err = svn_error_create(NULL, NULL, CUnicodeUtils::GetUTF8(CString(MAKEINTRESOURCE(IDS_SVN_USERCANCELLED))));
			return FALSE;
		}
		progress.Stop();
	}
	else
	{
		SVNPool subpool(pool);
		Err = svn_client_export4(NULL,		//no resulting revision needed
			srcPath.GetSVNApiPath(subpool),
			destPath.GetSVNApiPath(subpool),
			pegrev,
			revision,
			force,
			bIgnoreExternals,
			depth,
			eol.IsEmpty() ? NULL : (LPCSTR)CStringA(eol),
			m_pctx,
			subpool);
		if(Err != NULL)
		{
			return FALSE;
		}
	}
	return TRUE;
}

BOOL SVN::Switch(const CTSVNPath& path, const CTSVNPath& url, const SVNRev& revision, const SVNRev& pegrev, svn_depth_t depth, BOOL depthIsSticky, BOOL ignore_externals, BOOL allow_unver_obstruction)
{
	SVNPool subpool(pool);
	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_switch2(NULL,
							 path.GetSVNApiPath(subpool),
							 url.GetSVNApiPath(subpool),
							 pegrev,
							 revision,
							 depth,
							 depthIsSticky,
							 ignore_externals,
							 allow_unver_obstruction,
							 m_pctx,
							 subpool);
	if(Err != NULL)
	{
		return FALSE;
	}
	
	return TRUE;
}

BOOL SVN::Import(const CTSVNPath& path, const CTSVNPath& url, const CString& message, 
				 ProjectProperties * props, svn_depth_t depth, BOOL no_ignore, BOOL ignore_unknown,
				 const RevPropHash revProps)
{
	// the import command should use the mime-type file
	const char *mimetypes_file = NULL;
	svn_error_clear(Err);
	Err = NULL;
	svn_config_t * opt = (svn_config_t *)apr_hash_get (m_pctx->config, SVN_CONFIG_CATEGORY_CONFIG,
		APR_HASH_KEY_STRING);
	svn_config_get(opt, &mimetypes_file,
		SVN_CONFIG_SECTION_MISCELLANY,
		SVN_CONFIG_OPTION_MIMETYPES_FILE, FALSE);
	if (mimetypes_file && *mimetypes_file)
	{
		Err = svn_io_parse_mimetypes_file(&(m_pctx->mimetypes_map),
			mimetypes_file, pool);
		if (Err)
			return FALSE;
	}
	if (props)
		props->InsertAutoProps(opt);

	SVNPool subpool(pool);
	svn_commit_info_t *commit_info = svn_create_commit_info(subpool);
	m_pctx->log_msg_baton3 = logMessage(CUnicodeUtils::GetUTF8(message));
	apr_hash_t * revPropHash = MakeRevPropHash(revProps, subpool);
	Err = svn_client_import3(&commit_info,
							path.GetSVNApiPath(subpool),
							url.GetSVNApiPath(subpool),
							depth,
							no_ignore,
							ignore_unknown,
							revPropHash,
							m_pctx,
							subpool);
	m_pctx->log_msg_baton3 = logMessage("");
	if(Err != NULL)
	{
		return FALSE;
	}

	PostCommitErr.Empty();
	if (commit_info)
	{
		if (SVN_IS_VALID_REVNUM(commit_info->revision))
		{
			Notify(path, CTSVNPath(), svn_wc_notify_update_completed, svn_node_none, _T(""), 
					svn_wc_notify_state_unknown, svn_wc_notify_state_unknown, 
					commit_info->revision, NULL, svn_wc_notify_lock_state_unchanged, 
					_T(""), _T(""), NULL, NULL, pool);
		}
		if (commit_info->post_commit_err)
		{
			PostCommitErr = CUnicodeUtils::GetUnicode(commit_info->post_commit_err);
		}
	}

	return TRUE;
}

BOOL SVN::Merge(const CTSVNPath& path1, const SVNRev& revision1, const CTSVNPath& path2, const SVNRev& revision2, 
				const CTSVNPath& localPath, BOOL force, svn_depth_t depth, const CString& options,
				BOOL ignoreanchestry, BOOL dryrun, BOOL record_only)
{
	SVNPool subpool(pool);
	apr_array_header_t *opts;

	opts = svn_cstring_split (CUnicodeUtils::GetUTF8(options), " \t\n\r", TRUE, subpool);

	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_merge3(path1.GetSVNApiPath(subpool),
							revision1,
							path2.GetSVNApiPath(subpool),
							revision2,
							localPath.GetSVNApiPath(subpool),
							depth,
							ignoreanchestry,
							force,
							record_only,
							dryrun,
							opts,
							m_pctx,
							subpool);
	if(Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::PegMerge(const CTSVNPath& source, const SVNRevRangeArray& revrangearray, const SVNRev& pegrevision, 
				   const CTSVNPath& destpath, BOOL force, svn_depth_t depth, const CString& options,
				   BOOL ignoreancestry, BOOL dryrun, BOOL record_only)
{
	SVNPool subpool(pool);
	apr_array_header_t *opts;

	opts = svn_cstring_split (CUnicodeUtils::GetUTF8(options), " \t\n\r", TRUE, subpool);

	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_merge_peg3 (source.GetSVNApiPath(subpool),
		revrangearray.GetAprArray(subpool),
		pegrevision,
		destpath.GetSVNApiPath(subpool),
		depth,
		ignoreancestry,
		force,
		record_only,
		dryrun,
		opts,
		m_pctx,
		subpool);
	if (Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::MergeReintegrate(const CTSVNPath& source, const SVNRev& pegrevision, const CTSVNPath& wcpath, BOOL dryrun, const CString& options)
{
	SVNPool subpool(pool);
	apr_array_header_t *opts;

	opts = svn_cstring_split (CUnicodeUtils::GetUTF8(options), " \t\n\r", TRUE, subpool);

	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_merge_reintegrate(source.GetSVNApiPath(subpool),
		pegrevision,
		wcpath.GetSVNApiPath(subpool),
		dryrun,
		opts,
		m_pctx,
		subpool);
	if (Err != NULL)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::SuggestMergeSources(const CTSVNPath& targetpath, const SVNRev& revision, CTSVNPathList& sourceURLs)
{
	SVNPool subpool(pool);
	apr_array_header_t * sourceurls;

	svn_error_clear(Err);
	Err = NULL;
	sourceURLs.Clear();
	Err = svn_client_suggest_merge_sources(&sourceurls, 
											targetpath.GetSVNApiPath(subpool), 
											revision, 
											m_pctx, 
											subpool);

	if (Err != NULL)
	{
		return FALSE;
	}

	for (int i = 0; i < sourceurls->nelts; i++)
	{
		const char *path = (APR_ARRAY_IDX (sourceurls, i, const char*));
		sourceURLs.AddPath(CTSVNPath(CUnicodeUtils::GetUnicode(path)));
	}

	return TRUE;
}

BOOL SVN::CreatePatch(const CTSVNPath& path1, const SVNRev& revision1, const CTSVNPath& path2, const SVNRev& revision2, const CTSVNPath& relativeToDir, svn_depth_t depth, BOOL ignoreancestry, BOOL nodiffdeleted, BOOL ignorecontenttype,  const CString& options, bool bAppend, const CTSVNPath& outputfile)
{
	// to create a patch, we need to remove any custom diff tools which might be set in the config file
	svn_config_t * cfg = (svn_config_t *)apr_hash_get (m_pctx->config, SVN_CONFIG_CATEGORY_CONFIG, APR_HASH_KEY_STRING);
	const char * value;
	svn_config_get(cfg, &value, SVN_CONFIG_SECTION_HELPERS, SVN_CONFIG_OPTION_DIFF_CMD, NULL);
	CStringA diffCmd = CStringA(value);
	svn_config_get(cfg, &value, SVN_CONFIG_SECTION_HELPERS, SVN_CONFIG_OPTION_DIFF3_CMD, NULL);
	CStringA diff3Cmd = CStringA(value);

	svn_config_set(cfg, SVN_CONFIG_SECTION_HELPERS, SVN_CONFIG_OPTION_DIFF_CMD, NULL);
	svn_config_set(cfg, SVN_CONFIG_SECTION_HELPERS, SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

	BOOL bRet = Diff(path1, revision1, path2, revision2, relativeToDir, depth, ignoreancestry, nodiffdeleted, ignorecontenttype, options, bAppend, outputfile, CTSVNPath());
	svn_config_set(cfg, SVN_CONFIG_SECTION_HELPERS, SVN_CONFIG_OPTION_DIFF_CMD, (LPCSTR)diffCmd);
	svn_config_set(cfg, SVN_CONFIG_SECTION_HELPERS, SVN_CONFIG_OPTION_DIFF3_CMD, (LPCSTR)diff3Cmd);
	return bRet;
}

BOOL SVN::Diff(const CTSVNPath& path1, const SVNRev& revision1, const CTSVNPath& path2, const SVNRev& revision2, const CTSVNPath& relativeToDir, svn_depth_t depth, BOOL ignoreancestry, BOOL nodiffdeleted, BOOL ignorecontenttype,  const CString& options, bool bAppend, const CTSVNPath& outputfile)
{
	return Diff(path1, revision1, path2, revision2, relativeToDir, depth, ignoreancestry, nodiffdeleted, ignorecontenttype, options, bAppend, outputfile, CTSVNPath());
}

BOOL SVN::Diff(const CTSVNPath& path1, const SVNRev& revision1, const CTSVNPath& path2, const SVNRev& revision2, const CTSVNPath& relativeToDir, svn_depth_t depth, BOOL ignoreancestry, BOOL nodiffdeleted, BOOL ignorecontenttype,  const CString& options, bool bAppend, const CTSVNPath& outputfile, const CTSVNPath& errorfile)
{
	BOOL del = FALSE;
	apr_file_t * outfile;
	apr_file_t * errfile;
	apr_array_header_t *opts;

	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;

	opts = svn_cstring_split (CUnicodeUtils::GetUTF8(options), " \t\n\r", TRUE, localpool);

	apr_int32_t flags = APR_WRITE | APR_CREATE | APR_BINARY;
	if (bAppend)
		flags |= APR_APPEND;
	else
		flags |= APR_TRUNCATE;
	Err = svn_io_file_open (&outfile, outputfile.GetSVNApiPath(localpool),
							flags,
							APR_OS_DEFAULT, localpool);
	if (Err)
		return FALSE;

	CTSVNPath workingErrorFile;
	if (errorfile.IsEmpty())
	{
		workingErrorFile = CTempFiles::Instance().GetTempFilePath(true);
		del = TRUE;
	}
	else
	{
		workingErrorFile = errorfile;
	}

	Err = svn_io_file_open (&errfile, workingErrorFile.GetSVNApiPath(localpool),
							APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BINARY,
							APR_OS_DEFAULT, localpool);
	if (Err)
		return FALSE;

	Err = svn_client_diff4 (opts,
						   path1.GetSVNApiPath(localpool),
						   revision1,
						   path2.GetSVNApiPath(localpool),
						   revision2,
						   relativeToDir.GetSVNApiPath(localpool),
						   depth,
						   ignoreancestry,
						   nodiffdeleted,
						   ignorecontenttype,
						   APR_LOCALE_CHARSET,
						   outfile,
						   errfile,
						   NULL,		// we don't deal with change lists when diffing
						   m_pctx,
						   localpool);
	if (Err)
	{
		return FALSE;
	}
	if (del)
	{
		svn_io_remove_file (workingErrorFile.GetSVNApiPath(localpool), localpool);
	}
	return TRUE;
}

BOOL SVN::PegDiff(const CTSVNPath& path, const SVNRev& pegrevision, const SVNRev& startrev, const SVNRev& endrev, const CTSVNPath& relativeToDir, svn_depth_t depth, BOOL ignoreancestry, BOOL nodiffdeleted, BOOL ignorecontenttype, const CString& options, const CTSVNPath& outputfile)
{
	return PegDiff(path, pegrevision, startrev, endrev, relativeToDir, depth, ignoreancestry, nodiffdeleted, ignorecontenttype, options, outputfile, CTSVNPath());
}

BOOL SVN::PegDiff(const CTSVNPath& path, const SVNRev& pegrevision, const SVNRev& startrev, const SVNRev& endrev, const CTSVNPath& relativeToDir, svn_depth_t depth, BOOL ignoreancestry, BOOL nodiffdeleted, BOOL ignorecontenttype, const CString& options, const CTSVNPath& outputfile, const CTSVNPath& errorfile)
{
	BOOL del = FALSE;
	apr_file_t * outfile;
	apr_file_t * errfile;
	apr_array_header_t *opts;

	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;

	opts = svn_cstring_split (CUnicodeUtils::GetUTF8(options), " \t\n\r", TRUE, localpool);

	Err = svn_io_file_open (&outfile, outputfile.GetSVNApiPath(localpool),
		APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BINARY,
		APR_OS_DEFAULT, localpool);
	if (Err)
		return FALSE;

	CTSVNPath workingErrorFile;
	if (errorfile.IsEmpty())
	{
		workingErrorFile = CTempFiles::Instance().GetTempFilePath(true);
		del = TRUE;
	}
	else
	{
		workingErrorFile = errorfile;
	}

	Err = svn_io_file_open (&errfile, workingErrorFile.GetSVNApiPath(localpool),
		APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BINARY,
		APR_OS_DEFAULT, localpool);
	if (Err)
		return FALSE;

	Err = svn_client_diff_peg4 (opts,
		path.GetSVNApiPath(localpool),
		pegrevision,
		startrev,
		endrev,
		relativeToDir.GetSVNApiPath(localpool),
		depth,
		ignoreancestry,
		nodiffdeleted,
		ignorecontenttype,
		APR_LOCALE_CHARSET,
		outfile,
		errfile,
		NULL, // we don't deal with change lists when diffing
		m_pctx,
		localpool);
	if (Err)
	{
		return FALSE;
	}
	if (del)
	{
		svn_io_remove_file (workingErrorFile.GetSVNApiPath(localpool), localpool);
	}
	return TRUE;
}

bool SVN::DiffSummarize(const CTSVNPath& path1, const SVNRev& rev1, const CTSVNPath& path2, const SVNRev& rev2, svn_depth_t depth, bool ignoreancestry)
{
	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_diff_summarize2(path1.GetSVNApiPath(localpool), rev1,
									path2.GetSVNApiPath(localpool), rev2,
									depth, ignoreancestry, NULL, 
									summarize_func, this,
									m_pctx, localpool);
	if(Err != NULL)
	{
		return false;
	}
	return true;
}

bool SVN::DiffSummarizePeg(const CTSVNPath& path, const SVNRev& peg, const SVNRev& rev1, const SVNRev& rev2, svn_depth_t depth, bool ignoreancestry)
{
	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_diff_summarize_peg2(path.GetSVNApiPath(localpool), peg, rev1, rev2,
										depth, ignoreancestry, NULL, 
										summarize_func, this,
										m_pctx, localpool);
	if(Err != NULL)
	{
		return false;
	}
	return true;
}

LogCache::CCachedLogInfo* SVN::GetLogCache (const CTSVNPath& path)
{
    if (!LogCache::CSettings::GetEnabled())
        return NULL;

    CString uuid;
    CString root = GetLogCachePool()->GetRepositoryInfo().GetRepositoryRootAndUUID (path, uuid);
    return GetLogCachePool()->GetCache (uuid, root);
}

BOOL SVN::ReceiveLog(const CTSVNPathList& pathlist, const SVNRev& revisionPeg, const SVNRev& revisionStart, const SVNRev& revisionEnd, int limit, BOOL strict, BOOL withMerges, bool refresh)
{
	svn_error_clear(Err);
	Err = NULL;
	try
	{
		SVNPool localpool(pool);

        CSVNLogQuery svnQuery (m_pctx, localpool);
		CCacheLogQuery cacheQuery (GetLogCachePool(), &svnQuery);
		CCacheLogQuery refreshQuery (*this, &svnQuery);

		ILogQuery* query = logCachePool->IsEnabled()
						 ? refresh ? static_cast<ILogQuery*>(&refreshQuery)
                                   : static_cast<ILogQuery*>(&cacheQuery)
						 : static_cast<ILogQuery*>(&svnQuery);

		query->Log ( pathlist
				   , revisionPeg
				   , revisionStart
				   , revisionEnd
				   , limit
				   , strict != FALSE
				   , this
                   , true
                   , withMerges != FALSE
                   , true
                   , false
                   , TRevPropNames());

        if (refresh && logCachePool->IsEnabled())
        {
            // handle cache refresh results

            if (refreshQuery.GotAnyData())
            {
                refreshQuery.UpdateCache (GetLogCachePool());
            }
            else
            {
                // no connection to the repository but also not canceled 
                // (no exception thrown) -> re-run from cache

                return ReceiveLog ( pathlist
                                  , revisionPeg, revisionStart, revisionEnd
                                  , limit, strict, withMerges
                                  , false);
            }
        }
	}
	catch (SVNError& e)
	{
		Err = svn_error_create (e.GetCode(), NULL, e.GetMessage());
		return FALSE;
	}

	return TRUE;
}

BOOL SVN::Cat(const CTSVNPath& url, const SVNRev& pegrevision, const SVNRev& revision, const CTSVNPath& localpath)
{
	apr_file_t * file;
	svn_stream_t * stream;
	apr_status_t status;
	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;

	CTSVNPath fullLocalPath(localpath);
	if (fullLocalPath.IsDirectory())
	{
		fullLocalPath.AppendPathString(url.GetFileOrDirectoryName());
	}
	::DeleteFile(fullLocalPath.GetWinPath());

	status = apr_file_open(&file, fullLocalPath.GetSVNApiPath(localpool), APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT, localpool);
	if (status)
	{
		Err = svn_error_wrap_apr(status, NULL);
		return FALSE;
	}
	stream = svn_stream_from_aprfile2(file, true, localpool);

	Err = svn_client_cat2(stream, url.GetSVNApiPath(localpool), pegrevision, revision, m_pctx, localpool);

	apr_file_close(file);
	if (Err != NULL)
		return FALSE;
	return TRUE;
}

BOOL SVN::CreateRepository(const CTSVNPath& path, const CString& fstype)
{
	svn_repos_t * repo;
	svn_error_t * err;
	apr_hash_t *config;

	SVNPool localpool;

	apr_hash_t *fs_config = apr_hash_make (localpool);;

	apr_hash_set (fs_config, SVN_FS_CONFIG_BDB_TXN_NOSYNC,
		APR_HASH_KEY_STRING, "0");

	apr_hash_set (fs_config, SVN_FS_CONFIG_BDB_LOG_AUTOREMOVE,
		APR_HASH_KEY_STRING, "1");

	err = svn_config_get_config (&config, g_pConfigDir, localpool);
	if (err != NULL)
	{
		svn_error_clear(err);
		return FALSE;
	}
	const char * fs_type = apr_pstrdup(localpool, CStringA(fstype));
	apr_hash_set (fs_config, SVN_FS_CONFIG_FS_TYPE,
		APR_HASH_KEY_STRING,
		fs_type);
	err = svn_repos_create(&repo, path.GetSVNApiPath(localpool), NULL, NULL, config, fs_config, localpool);
	if (err != NULL)
	{
		svn_error_clear(err);
		return FALSE;
	}
	return TRUE;
}

BOOL SVN::Blame(const CTSVNPath& path, const SVNRev& startrev, const SVNRev& endrev, const SVNRev& peg, const CString& diffoptions, bool ignoremimetype, bool includemerge)
{
	svn_error_clear(Err);
	Err = NULL;
	SVNPool subpool(pool);
	apr_array_header_t *opts;
	svn_diff_file_options_t * options = svn_diff_file_options_create(subpool);
	opts = svn_cstring_split (CUnicodeUtils::GetUTF8(diffoptions), " \t\n\r", TRUE, subpool);
	svn_error_clear(svn_diff_file_options_parse(options, opts, subpool));

	// Subversion < 1.4 silently changed a revision WC to BASE. Due to a bug
	// report this was changed: now Subversion returns an error 'not implemented'
	// since it actually blamed the BASE file and not the working copy file.
	// Until that's implemented, we 'fall back' here to the old behavior and
	// just change and REV_WC to REV_BASE.
	SVNRev rev1 = startrev;
	SVNRev rev2 = endrev;
	if (rev1.IsWorking())
		rev1 = SVNRev::REV_BASE;
	if (rev2.IsWorking())
		rev2 = SVNRev::REV_BASE;
	Err = svn_client_blame4 ( path.GetSVNApiPath(subpool),
							 peg,
							 rev1,  
							 rev2,
							 options,
							 ignoremimetype,
							 includemerge,
							 blameReceiver,  
							 (void *)this,  
							 m_pctx,  
							 subpool);
	if ((Err != 0)&&((Err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)||(Err->apr_err == SVN_ERR_FS_NOT_FOUND)||(Err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES))&&(includemerge))
	{
		svn_error_clear(Err);
		Err = NULL;
		Err = svn_client_blame4 (   path.GetSVNApiPath(subpool),
									peg,
									rev1,  
									rev2,
									options,
									ignoremimetype,
									false,
									blameReceiver,  
									(void *)this,  
									m_pctx,  
									subpool);
	}

	if(Err != NULL)
	{
		return FALSE;
	}
	return TRUE;
}

svn_error_t* SVN::blameReceiver(void *baton, 
								apr_int64_t line_no, 
								svn_revnum_t revision, 
								const char *author, 
								const char *date, 
								svn_revnum_t merged_revision, 
								const char *merged_author, 
								const char *merged_date, 
								const char *merged_path, 
								const char *line, 
								apr_pool_t *pool)
{
	svn_error_t * error = NULL;
	CString author_native, merged_author_native;
	CString merged_path_native;
	CStringA line_native;
	TCHAR date_native[SVN_DATE_BUFFER] = {0};
	TCHAR merged_date_native[SVN_DATE_BUFFER] = {0};

	SVN * svn = (SVN *)baton;

	if (author)
		author_native = CUnicodeUtils::GetUnicode(author);
	if (merged_author)
		merged_author_native = CUnicodeUtils::GetUnicode(merged_author);
	if (merged_path)
		merged_path_native = CUnicodeUtils::GetUnicode(merged_path);
	if (line)
		line_native = line;

	if (merged_date && merged_date[0])
	{
		// Convert date to a format for humans.
		apr_time_t time_temp;

		error = svn_time_from_cstring (&time_temp, merged_date, pool);
		if (error)
			return error;

		formatDate(merged_date_native, time_temp, true);
	}
	else
		_tcscat_s(merged_date_native, SVN_DATE_BUFFER, _T("(no date)"));

	if (date && date[0])
	{
		// Convert date to a format for humans.
		apr_time_t time_temp;

		error = svn_time_from_cstring (&time_temp, date, pool);
		if (error)
			return error;

		formatDate(date_native, time_temp, true);
	}
	else
		_tcscat_s(date_native, SVN_DATE_BUFFER, _T("(no date)"));

	if (!svn->BlameCallback((LONG)line_no, revision, author_native, date_native, merged_revision, merged_author_native, merged_date_native, merged_path_native, line_native))
	{
		return svn_error_create(SVN_ERR_CANCELLED, NULL, "error in blame callback");
	}
	return error;
}

BOOL SVN::Lock(const CTSVNPathList& pathList, BOOL bStealLock, const CString& comment /* = CString( */)
{
	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_lock(pathList.MakePathArray(pool), CUnicodeUtils::GetUTF8(comment), bStealLock, m_pctx, pool);
	return (Err == NULL);	
}

BOOL SVN::Unlock(const CTSVNPathList& pathList, BOOL bBreakLock)
{
	svn_error_clear(Err);
	Err = NULL;
	Err = svn_client_unlock(pathList.MakePathArray(pool), bBreakLock, m_pctx, pool);
	return (Err == NULL);
}

svn_error_t* SVN::summarize_func(const svn_client_diff_summarize_t *diff, void *baton, apr_pool_t * /*pool*/)
{
	SVN * svn = (SVN *)baton;
	if (diff)
	{
		CTSVNPath path = CTSVNPath(CUnicodeUtils::GetUnicode(diff->path));
		return svn->DiffSummarizeCallback(path, diff->summarize_kind, !!diff->prop_changed, diff->node_kind);
	}
	return SVN_NO_ERROR;
}
svn_error_t* SVN::listReceiver(void* baton, const char* path, 
							   const svn_dirent_t *dirent, 
							   const svn_lock_t *lock, 
							   const char *abs_path, 
							   apr_pool_t * /*pool*/)
{
	SVN * svn = (SVN *)baton;
	svn->ReportList(CUnicodeUtils::GetUnicode(path), 
		dirent->kind,
		dirent->size,
		!!dirent->has_props,
		dirent->created_rev,
		dirent->time,
		CUnicodeUtils::GetUnicode(dirent->last_author),
		lock ? CUnicodeUtils::GetUnicode(lock->token) : CString(),
		lock ? CUnicodeUtils::GetUnicode(lock->owner) : CString(),
		lock ? CUnicodeUtils::GetUnicode(lock->comment) : CString(),
		lock ? !!lock->is_dav_comment : false,
		lock ? lock->creation_date : 0,
		lock ? lock->expiration_date : 0,
		CUnicodeUtils::GetUnicode(abs_path));
	svn_error_t * err = NULL;
	if (svn->Cancel())
	{
		CString temp;
		temp.LoadString(IDS_SVN_USERCANCELLED);
		err = svn_error_create(SVN_ERR_CANCELLED, NULL, CUnicodeUtils::GetUTF8(temp));
	}
	return err;
}

// implement ILogReceiver

void SVN::ReceiveLog ( LogChangedPathArray* changes
					 , svn_revnum_t rev
                     , const StandardRevProps* stdRevProps
                     , UserRevPropArray* /* userRevProps*/
                     , bool mergesFollow)
{
	// aggregate common change mask

	DWORD actions = 0;
	BOOL copies = FALSE;
    if (changes != NULL)
    {
	    for (INT_PTR i = 0, count = changes->GetCount(); i < count; ++i)
	    {
		    const LogChangedPath* change = changes->GetAt (i);
		    actions |= change->action;
		    copies |= change->lCopyFromRev != 0;
	    }
    }

	// convert time stamp to string

	TCHAR date_native[SVN_DATE_BUFFER] = {0};
    if (stdRevProps != NULL)
    {
        apr_time_t temp = stdRevProps->timeStamp;
	    formatDate (date_native, temp);
    }
    
	// check for user pressing "Cancel" somewhere

	cancel();

	// finally, use the log info (in a derived class specific way)

    static const CString emptyString;
	Log ( rev
		, stdRevProps == NULL ? emptyString : stdRevProps->author
		, date_native
        , stdRevProps == NULL ? emptyString : stdRevProps->message
		, changes
        , stdRevProps == NULL ? apr_time_t(0) : stdRevProps->timeStamp
        , changes == NULL ? 0 : static_cast<int>(changes->GetCount())
		, copies
		, actions
        , mergesFollow);
}

void SVN::notify( void *baton,
				 const svn_wc_notify_t *notify,
				 apr_pool_t *pool)
{
	SVN * svn = (SVN *)baton;

	CTSVNPath tsvnPath;
	tsvnPath.SetFromSVN(notify->path);
	CTSVNPath url;
	url.SetFromSVN(notify->url);

	CString mime;
	if (notify->mime_type)
		mime = CUnicodeUtils::GetUnicode(notify->mime_type);

	CString changelistname;
	if (notify->changelist_name)
		changelistname = CUnicodeUtils::GetUnicode(notify->changelist_name);

	CString propertyName;
	if (notify->prop_name)
		propertyName = CUnicodeUtils::GetUnicode(notify->prop_name);

	svn->Notify(tsvnPath, url, notify->action, notify->kind, 
				mime, notify->content_state, 
				notify->prop_state, notify->revision, 
				notify->lock, notify->lock_state, changelistname, propertyName, notify->merge_range, 
				notify->err, pool);
}

svn_error_t* SVN::conflict_resolver(svn_wc_conflict_result_t **result, 
							   const svn_wc_conflict_description_t *description, 
							   void *baton, 
							   apr_pool_t * pool)
{
	SVN * svn = (SVN *)baton;
	CString file;
	svn_wc_conflict_choice_t choice = svn->ConflictResolveCallback(description, file);
	*result = svn_wc_create_conflict_result(choice, file.IsEmpty() ? NULL : apr_pstrdup(pool, (const char*)CUnicodeUtils::GetUTF8(file)), pool);
	return SVN_NO_ERROR;
}

svn_error_t* SVN::cancel(void *baton)
{
	SVN * svn = (SVN *)baton;
	if ((svn->Cancel())||((svn->m_pProgressDlg)&&(svn->m_pProgressDlg->HasUserCancelled())))
	{
		CString temp;
		temp.LoadString(IDS_SVN_USERCANCELLED);
		return svn_error_create(SVN_ERR_CANCELLED, NULL, CUnicodeUtils::GetUTF8(temp));
	}
	return SVN_NO_ERROR;
}

void SVN::cancel()
{
	if (Cancel() || ((m_pProgressDlg != NULL) && (m_pProgressDlg->HasUserCancelled())))
	{
		CStringA message;
		message.LoadString (IDS_SVN_USERCANCELLED);
		throw SVNError (SVN_ERR_CANCELLED, message);
	}
}

void * SVN::logMessage (CStringA message, char * baseDirectory)
{
	message.Replace("\r", "");
	log_msg_baton3* baton = (log_msg_baton3 *) apr_palloc (pool, sizeof (*baton));
	baton->message = apr_pstrdup(pool, message);
	baton->base_dir = baseDirectory ? baseDirectory : "";

	baton->message_encoding = NULL;
	baton->tmpfile_left = NULL;
	baton->pool = pool;

	return baton;
}

void SVN::PathToUrl(CString &path)
{
	bool bUNC = false;
	path.Trim();
	if (path.Left(2).Compare(_T("\\\\"))==0)
		bUNC = true;
	// convert \ to /
	path.Replace('\\','/');
	path.TrimLeft('/');
	// prepend file://
	if (bUNC)
		path.Insert(0, _T("file://"));
	else
		path.Insert(0, _T("file:///"));
	path.TrimRight(_T("/\\"));			//remove trailing slashes
}

void SVN::UrlToPath(CString &url)
{
	// we have to convert paths like file:///c:/myfolder
	// to c:/myfolder
	// and paths like file:////mymachine/c/myfolder
	// to //mymachine/c/myfolder
	url.Trim();
	url.Replace('\\','/');
	url = url.Mid(7);	// "file://" has seven chars
	url.TrimLeft('/');
	// if we have a ':' that means the file:// url points to an absolute path
	// like file:///D:/Development/test
	// if we don't have a ':' we assume it points to an UNC path, and those
	// actually _need_ the slashes before the paths
	if ((url.Find(':')<0) && (url.Find('|')<0))
		url.Insert(0, _T("\\\\"));
	SVN::preparePath(url);
	// now we need to unescape the url
	url = CPathUtils::PathUnescape(url);
}

void SVN::preparePath(CString &path)
{
	path.Trim();
	path.TrimRight(_T("/\\"));			//remove trailing slashes
	path.Replace('\\','/');

	if (path.Left(10).CompareNoCase(_T("file://///"))==0)
	{
		if (path.Find('%')<0)
			path.Replace(_T("file://///"), _T("file://"));
		else
			path.Replace(_T("file://///"), _T("file:////"));
	}
	else if (path.Left(9).CompareNoCase(_T("file:////"))==0)
	{
		if (path.Find('%')<0)
			path.Replace(_T("file:////"), _T("file://"));
	}
}

svn_error_t* svn_cl__get_log_message(const char **log_msg,
									const char **tmp_file,
									const apr_array_header_t * /*commit_items*/,
									void *baton, 
									apr_pool_t * pool)
{
	log_msg_baton3 *lmb = (log_msg_baton3 *) baton;
	*tmp_file = NULL;
	if (lmb->message)
	{
		*log_msg = apr_pstrdup (pool, lmb->message);
	}

	return SVN_NO_ERROR;
}

CString SVN::GetURLFromPath(const CTSVNPath& path)
{
	const char * URL;
	if (path.IsUrl())
		return path.GetSVNPathString();
	if (!path.Exists())
		return _T("");
	svn_error_clear(Err);
	Err = NULL;
	SVNPool subpool(pool);
	Err = svn_client_url_from_path (&URL, path.GetSVNApiPath(subpool), subpool);
	if (Err)
		return _T("");
	if (URL==NULL)
		return _T("");
	return CString(URL);
}

CString SVN::GetUUIDFromPath(const CTSVNPath& path)
{
	const char * UUID;
	svn_error_clear(Err);
	Err = NULL;
	SVNPool subpool(pool);
	if (PathIsURL(path))
	{
		Err = svn_client_uuid_from_url(&UUID, path.GetSVNApiPath(subpool), m_pctx, subpool);
	}
	else
	{
		Err = get_uuid_from_target(&UUID, path.GetSVNApiPath(subpool));
	}
	if (Err)
		return _T("");
	if (UUID==NULL)
		return _T("");
	CString ret = CString(UUID);
	return ret;
}

svn_error_t * SVN::get_uuid_from_target (const char **UUID, const char *target)
{
	svn_wc_adm_access_t *adm_access;          
#pragma warning(push)
#pragma warning(disable: 4127)	// conditional expression is constant
	SVN_ERR (svn_wc_adm_probe_open3 (&adm_access, NULL, target,
		FALSE, 0, NULL, NULL, pool));
	SVN_ERR (svn_client_uuid_from_path(UUID, target, adm_access, m_pctx, pool));
	SVN_ERR (svn_wc_adm_close2 (adm_access, pool));
#pragma warning(pop)

	return SVN_NO_ERROR;
}

BOOL SVN::List(const CTSVNPath& url, const SVNRev& revision, const SVNRev& pegrev, svn_depth_t depth, bool fetchlocks)
{
	SVNPool subpool(pool);
	svn_error_clear(Err);
	Err = NULL;
	
	Err = svn_client_list2(url.GetSVNApiPath(subpool),
						  pegrev,
						  revision,
						  depth,
						  SVN_DIRENT_ALL,
						  fetchlocks,
						  listReceiver,
						  this,
						  m_pctx,
						  subpool);
	if (Err != NULL)
		return FALSE;
	return TRUE;
}

BOOL SVN::Relocate(const CTSVNPath& path, const CTSVNPath& from, const CTSVNPath& to, BOOL recurse)
{
	svn_error_clear(Err);
	Err = NULL;
	SVNPool subpool(pool);
	Err = svn_client_relocate(
				path.GetSVNApiPath(subpool), 
				from.GetSVNApiPath(subpool), 
				to.GetSVNApiPath(subpool), 
				recurse, m_pctx, subpool);
	if (Err != NULL)
		return FALSE;
	return TRUE;
}

BOOL SVN::IsRepository(const CTSVNPath& path)
{
	svn_error_clear(Err);
	Err = NULL;
	// The URL we get here is per definition properly encoded and escaped.
	svn_repos_t* pRepos;
	CString url = path.GetSVNPathString();
	url += _T("/");
	int pos = url.GetLength();
	while ((pos = url.ReverseFind('/'))>=0)
	{
		url = url.Left(pos);
		if (PathFileExists(url))
		{
			Err = svn_repos_open (&pRepos, CUnicodeUtils::GetUTF8(url), pool);
			if ((Err)&&(Err->apr_err == SVN_ERR_FS_BERKELEY_DB))
				return TRUE;
			if (Err == NULL)
				return TRUE;
		}
	}

	return FALSE;
}

CString SVN::GetRepositoryRoot(const CTSVNPath& url)
{
	const char * returl = NULL;

	SVNPool localpool(pool);
	svn_error_clear(Err);
    Err = NULL;

	// make sure the url is canonical.
	const char * goodurl = url.GetSVNApiPath(localpool);
	
    // use cached information, if allowed

	if (LogCache::CSettings::GetEnabled())
    {
        // look up in cached repository properties
        // (missing entries will be added automatically)

        CTSVNPath canonicalURL;
        canonicalURL.SetFromSVN (goodurl);

        CRepositoryInfo& cachedProperties = GetLogCachePool()->GetRepositoryInfo();

        CString result = cachedProperties.GetRepositoryRoot (canonicalURL);
        if (result.IsEmpty())
            assert (Err != NULL);

        return result;
    }
    else
    {
		Err = svn_client_root_url_from_path(&returl, goodurl, m_pctx, pool);
		if (Err)
			return _T("");

		return CString(returl);
	}
}

CString SVN::GetRepositoryRootAndUUID(const CTSVNPath& path, CString& sUUID)
{
	const char * returl;
	const char * uuid;
	svn_ra_session_t *ra_session;

	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;

	// empty the sUUID first
	sUUID.Empty();

	// make sure the url is canonical.

    const char * goodurl = NULL;
	if (!path.IsUrl())
    {
        // try to use local WC info to get root and UUID

	    SVNInfo info;
	    const SVNInfoData * baseInfo 
		    = info.GetFirstFileInfo (path, SVNRev(), SVNRev());
        if (baseInfo && !baseInfo->reposRoot.IsEmpty() && !baseInfo->reposUUID.IsEmpty())
        {
            sUUID = baseInfo->reposUUID;
            return baseInfo->reposRoot;
        }

        // fall back to RA layer

		Err = svn_client_url_from_path (&goodurl, path.GetSVNApiPath(localpool), localpool);
    }
	else
    {
		goodurl = path.GetSVNApiPath(localpool);
    }

	if (goodurl == NULL)
	{
		return _T("");
	}
	/* use subpool to create a temporary RA session */
	Err = svn_client_open_ra_session (&ra_session, goodurl, m_pctx, localpool);
	if (Err)
		return _T("");

	Err = svn_ra_get_repos_root2(ra_session, &returl, localpool);
	if (Err)
		return _T("");

	Err = svn_ra_get_uuid2(ra_session, &uuid, localpool);
	if (Err == NULL)
		sUUID = CString(uuid);

	return CString(returl);
}

svn_revnum_t SVN::GetHEADRevision(const CTSVNPath& path)
{
	svn_ra_session_t *ra_session;
	const char * urla;
	svn_revnum_t rev;

	SVNPool localpool(pool);
	svn_error_clear(Err);
    Err = NULL;

	if (!path.IsUrl())
		Err = svn_client_url_from_path (&urla, path.GetSVNApiPath(localpool), localpool);
	else
	{
		// make sure the url is canonical.
		const char * goodurl = path.GetSVNApiPath(localpool);
		urla = goodurl;
	}
	if (Err)
		return -1;
	/* use subpool to create a temporary RA session */
	Err = svn_client_open_ra_session (&ra_session, urla, m_pctx, localpool);
	if (Err)
		return -1;

	Err = svn_ra_get_latest_revnum(ra_session, &rev, localpool);
	if (Err)
		return -1;
	return rev;
}

BOOL SVN::GetRootAndHead(const CTSVNPath& path, CTSVNPath& url, svn_revnum_t& rev)
{
	svn_ra_session_t *ra_session;
	const char * urla;
	const char * returl;

	SVNPool localpool(pool);
	svn_error_clear(Err);
    Err = NULL;

	if (!path.IsUrl())
		Err = svn_client_url_from_path (&urla, path.GetSVNApiPath(localpool), localpool);
	else
	{
		// make sure the url is canonical.
		urla = path.GetSVNApiPath(localpool);
	}

	if (Err)
		return FALSE;

    // use cached information, if allowed

	if (LogCache::CSettings::GetEnabled())
    {
        // look up in cached repository properties
        // (missing entries will be added automatically)

        CTSVNPath canonicalURL;
        canonicalURL.SetFromSVN (urla);

        CRepositoryInfo& cachedProperties = GetLogCachePool()->GetRepositoryInfo();
        CString uuid;
        url.SetFromSVN (cachedProperties.GetRepositoryRootAndUUID (path, uuid));
        if (url.IsEmpty())
        {
            assert (Err != NULL);
        }
        else
        {
            rev = cachedProperties.GetHeadRevision (uuid, canonicalURL);
            if ((rev == NO_REVISION) && (Err == NULL))
            {
                Err = svn_client_open_ra_session (&ra_session, urla, m_pctx, localpool);
                if (Err)
                    return FALSE;

                Err = svn_ra_get_latest_revnum(ra_session, &rev, localpool);
                if (Err)
                    return FALSE;
            }
        }

        return Err == NULL ? TRUE : FALSE;
    }
    else
    {
        // non-cached access

	    /* use subpool to create a temporary RA session */

	    Err = svn_client_open_ra_session (&ra_session, urla, m_pctx, localpool);
	    if (Err)
		    return FALSE;

	    Err = svn_ra_get_latest_revnum(ra_session, &rev, localpool);
	    if (Err)
		    return FALSE;

	    Err = svn_ra_get_repos_root2(ra_session, &returl, localpool);
	    if (Err)
		    return FALSE;
    		
	    url.SetFromSVN(CUnicodeUtils::GetUnicode(returl));
    }
	
	return TRUE;
}

BOOL SVN::GetLocks(const CTSVNPath& url, std::map<CString, SVNLock> * locks)
{
	svn_ra_session_t *ra_session;

	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;

	apr_hash_t * hash = apr_hash_make(localpool);

	/* use subpool to create a temporary RA session */
	Err = svn_client_open_ra_session (&ra_session, url.GetSVNApiPath(localpool), m_pctx, localpool);
	if (Err != NULL)
		return FALSE;

	Err = svn_ra_get_locks(ra_session, &hash, "", localpool);
	if (Err != NULL)
		return FALSE;
	apr_hash_index_t *hi;
	svn_lock_t* val;
	const char* key;
	for (hi = apr_hash_first(localpool, hash); hi; hi = apr_hash_next(hi)) 
	{
		apr_hash_this(hi, (const void**)&key, NULL, (void**)&val);
		if (val)
		{
			SVNLock lock;
			lock.comment = CUnicodeUtils::GetUnicode(val->comment);
			lock.creation_date = val->creation_date/1000000L;
			lock.expiration_date = val->expiration_date/1000000L;
			lock.owner = CUnicodeUtils::GetUnicode(val->owner);
			lock.path = CUnicodeUtils::GetUnicode(val->path);
			lock.token = CUnicodeUtils::GetUnicode(val->token);
			CString sKey = CUnicodeUtils::GetUnicode(key);
			(*locks)[sKey] = lock;
		}
	}
	return TRUE;
}

BOOL SVN::GetWCRevisionStatus(const CTSVNPath& wcpath, bool bCommitted, svn_revnum_t& minrev, svn_revnum_t& maxrev, bool& switched, bool& modified, bool& sparse)
{
	SVNPool localpool(pool);
	svn_error_clear(Err);
	Err = NULL;

	svn_wc_revision_status_t * revstatus = NULL;
	Err = svn_wc_revision_status(&revstatus, wcpath.GetSVNApiPath(localpool), NULL, bCommitted, SVN::cancel, this, localpool);
	if ((Err)||(revstatus == NULL))
	{
		minrev = 0;
		maxrev = 0;
		switched = false;
		modified = false;
		sparse = false;
		return FALSE;
	}
	minrev = revstatus->min_rev;
	maxrev = revstatus->max_rev;
	switched = !!revstatus->switched;
	modified = !!revstatus->modified;
	sparse = !!revstatus->sparse_checkout;
	return TRUE;
}

svn_revnum_t SVN::RevPropertySet(const CString& sName, const CString& sValue, const CString& sOldValue, const CTSVNPath& URL, const SVNRev& rev)
{
	svn_revnum_t set_rev;
	svn_string_t*	pval = NULL;
	svn_string_t*	pval2 = NULL;
	svn_error_clear(Err);
	Err = NULL;

	CStringA sValueA = CUnicodeUtils::GetUTF8(sValue);
	sValueA.Replace("\r", "");
	pval = svn_string_create(sValueA, pool);
	if (!sOldValue.IsEmpty())
		pval2 = svn_string_create (CUnicodeUtils::GetUTF8(sOldValue), pool);

	Err = svn_client_revprop_set2(CUnicodeUtils::GetUTF8(sName), 
									pval, 
									pval2,
									URL.GetSVNApiPath(pool), 
									rev, 
									&set_rev, 
									FALSE, 
									m_pctx, 
									pool);
	if (Err)
		return 0;
	return set_rev;
}

CString SVN::RevPropertyGet(const CString& sName, const CTSVNPath& URL, const SVNRev& rev)
{
	svn_string_t *propval;
	svn_revnum_t set_rev;
	svn_error_clear(Err);
	Err = NULL;

	Err = svn_client_revprop_get(CUnicodeUtils::GetUTF8(sName), &propval, URL.GetSVNApiPath(pool), rev, &set_rev, m_pctx, pool);
	if (Err)
		return _T("");
	if (propval==NULL)
		return _T("");
	if (propval->len <= 0)
		return _T("");
	return CUnicodeUtils::GetUnicode(propval->data);
}

CTSVNPath SVN::GetPristinePath(const CTSVNPath& wcPath)
{
	svn_error_t * err;
	SVNPool localpool;

	const char* pristinePath = NULL;
	CTSVNPath returnPath;

#pragma warning(push)
#pragma warning(disable: 4996)	// deprecated warning
	// note: the 'new' function would be svn_wc_get_pristine_contents(), but that
	// function returns a stream instead of a path. Since we don't need a stream
	// but really a path here, that function is of no use and would require us
	// to create a temp file and copy the original contents to that temp file.
	// 
	// We can't pass a stream to e.g. TortoiseMerge for diffing, that's why we
	// need a *path* and not a stream.
	err = svn_wc_get_pristine_copy_path(svn_path_internal_style(wcPath.GetSVNApiPath(localpool), localpool), &pristinePath, localpool);
#pragma warning(pop)

	if (err != NULL)
	{
		svn_error_clear(err);
		return returnPath;
	}
	if (pristinePath != NULL)
	{
		returnPath.SetFromSVN(pristinePath);
	}
	return returnPath;
}

BOOL SVN::GetTranslatedFile(CTSVNPath& sTranslatedFile, const CTSVNPath& sFile, BOOL bForceRepair /*= TRUE*/)
{
	svn_wc_adm_access_t *adm_access;          
	svn_error_t * err;
	SVNPool localpool;

	const char * translatedPath = NULL;
	const char * originPath = sFile.GetSVNApiPath(localpool);
	err = svn_wc_adm_probe_open3 (&adm_access, NULL, originPath, FALSE, 0, NULL, NULL, localpool);
	if (err)
	{
		svn_error_clear(err);
		return FALSE;
	}
	err = svn_wc_translated_file2((const char **)&translatedPath, originPath, originPath, adm_access, SVN_WC_TRANSLATE_TO_NF | (bForceRepair ? SVN_WC_TRANSLATE_FORCE_EOL_REPAIR : 0), localpool);
	svn_wc_adm_close2(adm_access, localpool);
	if (err)
	{
		svn_error_clear(err);
		return FALSE;
	}

	sTranslatedFile.SetFromUnknown(CUnicodeUtils::GetUnicode(translatedPath));
	return (translatedPath != originPath);
}

BOOL SVN::PathIsURL(const CTSVNPath& path)
{
	return svn_path_is_url(CUnicodeUtils::GetUTF8(path.GetSVNPathString()));
} 

void SVN::formatDate(TCHAR date_native[], apr_time_t& date_svn, bool force_short_fmt)
{
	if (date_svn == NULL)
	{
		_tcscpy_s(date_native, SVN_DATE_BUFFER, _T("(no date)"));
		return;
	}

	FILETIME ft = {0};
	AprTimeToFileTime(&ft, date_svn);
	formatDate(date_native, ft, force_short_fmt);
}

void SVN::formatDate(TCHAR date_native[], FILETIME& filetime, bool force_short_fmt)
{
	date_native[0] = '\0';

	// Convert UTC to local time
	SYSTEMTIME systemtime;
	VERIFY( FileTimeToSystemTime(&filetime,&systemtime) );
	
	SYSTEMTIME localsystime;
	VERIFY( SystemTimeToTzSpecificLocalTime(NULL, &systemtime,&localsystime));

	TCHAR timebuf[SVN_DATE_BUFFER] = {0};
	TCHAR datebuf[SVN_DATE_BUFFER] = {0};

	LCID locale = s_useSystemLocale ? MAKELCID(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), SORT_DEFAULT) : s_locale;

	if (force_short_fmt || CRegDWORD(_T("Software\\TortoiseSVN\\LogDateFormat")) == 1)
	{
		GetDateFormat(locale, DATE_SHORTDATE, &localsystime, NULL, datebuf, SVN_DATE_BUFFER);
		GetTimeFormat(locale, 0, &localsystime, NULL, timebuf, SVN_DATE_BUFFER);
		_tcsncat_s(date_native, SVN_DATE_BUFFER, datebuf, SVN_DATE_BUFFER);
		_tcsncat_s(date_native, SVN_DATE_BUFFER, _T(" "), SVN_DATE_BUFFER);
		_tcsncat_s(date_native, SVN_DATE_BUFFER, timebuf, SVN_DATE_BUFFER);
	}
	else
	{
		GetDateFormat(locale, DATE_LONGDATE, &localsystime, NULL, datebuf, SVN_DATE_BUFFER);
		GetTimeFormat(locale, 0, &localsystime, NULL, timebuf, SVN_DATE_BUFFER);
		_tcsncat_s(date_native, SVN_DATE_BUFFER, timebuf, SVN_DATE_BUFFER);
		_tcsncat_s(date_native, SVN_DATE_BUFFER, _T(", "), SVN_DATE_BUFFER);
		_tcsncat_s(date_native, SVN_DATE_BUFFER, datebuf, SVN_DATE_BUFFER);
	}
}

CString SVN::formatDate(apr_time_t& date_svn)
{
	apr_time_exp_t exploded_time = {0};
	
    SYSTEMTIME systime = {0,0,0,0,0,0,0,0};
    TCHAR datebuf[SVN_DATE_BUFFER] = {0};

	LCID locale = s_useSystemLocale ? MAKELCID(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), SORT_DEFAULT) : s_locale;

	try
	{
		apr_time_exp_lt (&exploded_time, date_svn);

		systime.wDay = (WORD)exploded_time.tm_mday;
		systime.wDayOfWeek = (WORD)exploded_time.tm_wday;
		systime.wMonth = (WORD)exploded_time.tm_mon+1;
		systime.wYear = (WORD)exploded_time.tm_year+1900;

		GetDateFormat(locale, DATE_SHORTDATE, &systime, NULL, datebuf, SVN_DATE_BUFFER);
	}
	catch ( ... )
	{
		_tcscpy_s(datebuf, SVN_DATE_BUFFER, _T("(no date)"));
	}
	
    return datebuf;
}

CString SVN::formatTime (apr_time_t& date_svn)
{
	apr_time_exp_t exploded_time = {0};
	
    SYSTEMTIME systime = {0,0,0,0,0,0,0,0};
    TCHAR timebuf[SVN_DATE_BUFFER] = {0};

	LCID locale = s_useSystemLocale ? MAKELCID(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), SORT_DEFAULT) : s_locale;

	try
	{
		apr_time_exp_lt (&exploded_time, date_svn);

		systime.wDay = (WORD)exploded_time.tm_mday;
		systime.wDayOfWeek = (WORD)exploded_time.tm_wday;
		systime.wHour = (WORD)exploded_time.tm_hour;
		systime.wMilliseconds = (WORD)(exploded_time.tm_usec/1000);
		systime.wMinute = (WORD)exploded_time.tm_min;
		systime.wMonth = (WORD)exploded_time.tm_mon+1;
		systime.wSecond = (WORD)exploded_time.tm_sec;
		systime.wYear = (WORD)exploded_time.tm_year+1900;

		GetTimeFormat(locale, 0, &systime, NULL, timebuf, SVN_DATE_BUFFER);
	}
	catch ( ... )
	{
		_tcscpy_s(timebuf, SVN_DATE_BUFFER, _T("(no time)"));
	}

    return timebuf;
}


CString SVN::MakeUIUrlOrPath(const CStringA& UrlOrPath)
{
	CString url;
	if (svn_path_is_url(UrlOrPath))
	{
		url = CUnicodeUtils::GetUnicode(CPathUtils::PathUnescape(UrlOrPath));
	}
	else
		url = CUnicodeUtils::GetUnicode(UrlOrPath);
	return url;
}

BOOL SVN::EnsureConfigFile()
{
	svn_error_t * err;
	SVNPool localpool;
	err = svn_config_ensure(NULL, localpool);
	if (err)
	{
		svn_error_clear(err);
		return FALSE;
	}
	return TRUE;
}

CString SVN::GetOptionsString(BOOL bIgnoreEOL, BOOL bIgnoreSpaces, BOOL bIgnoreAllSpaces)
{
	CString opts;
	if (bIgnoreEOL)
		opts += _T("--ignore-eol-style ");
	if (bIgnoreAllSpaces)
		opts += _T("-w");
	else if (bIgnoreSpaces)
		opts += _T("-b");
	opts.Trim();
	return opts;
}

CString SVN::GetOptionsString(BOOL bIgnoreEOL, svn_diff_file_ignore_space_t space)
{
	CString opts;
	if (bIgnoreEOL)
		opts += _T("--ignore-eol-style ");
	switch (space)
	{
	case svn_diff_file_ignore_space_change:
		opts += _T("-b");
		break;
	case svn_diff_file_ignore_space_all:
		opts += _T("-w");
		break;
	}
	opts.Trim();
	return opts;
}

/**
 * Returns the log cache pool singleton. You will need that to 
 * create \c CCacheLogQuery instances.
 */
LogCache::CLogCachePool* SVN::GetLogCachePool() 
{
    if (logCachePool.get() == NULL)
    {
        CString cacheFolder = CPathUtils::GetAppDataDirectory()+_T("logcache\\");
        logCachePool.reset (new LogCache::CLogCachePool (*this, cacheFolder));
    }

    return logCachePool.get();
}

/**
 * Returns the status of the encapsulated \ref SVNPrompt instance.
 */
bool SVN::PromptShown() const
{
    return m_prompt.PromptShown();
}

/** 
 * Set the parent window of an authentication prompt dialog
 */
void SVN::SetPromptParentWindow(HWND hWnd)
{
	m_prompt.SetParentWindow(hWnd);
}
/** 
 * Set the MFC Application object for a prompt dialog
 */
void SVN::SetPromptApp(CWinApp* pWinApp)
{
	m_prompt.SetApp(pWinApp);
}

apr_array_header_t * SVN::MakeCopyArray(const CTSVNPathList& pathList, const SVNRev& rev, const SVNRev& pegrev)
{
	apr_array_header_t * sources = apr_array_make(pool, pathList.GetCount(),
		sizeof(svn_client_copy_source_t *));

	for (int nItem = 0; nItem < pathList.GetCount(); ++nItem)
	{
		const char *target = apr_pstrdup (pool, pathList[nItem].GetSVNApiPath(pool));
		svn_client_copy_source_t *source = (svn_client_copy_source_t*)apr_palloc(pool, sizeof(*source));
		source->path = target;
		source->revision = pegrev;
		source->peg_revision = rev;
		APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = source;
	}
	return sources;
}

apr_array_header_t * SVN::MakeChangeListArray(const CStringArray& changelists, apr_pool_t * pool)
{
	// passing NULL if there are no change lists will work only partially: the subversion library
	// in that case executes the command, but fails to remove the existing change lists from the files
	// if 'keep change lists is set to false.
	// We therefore have to create an empty array instead of passing NULL, only then the
	// change lists are removed properly.
	int count = changelists.GetCount();
    // special case: the changelist array contains one empty string
    if ((changelists.GetCount() == 1)&&(changelists[0].IsEmpty()))
        count = 0;

	apr_array_header_t * arr = apr_array_make (pool, count, sizeof(const char *));

	if (count == 0)
		return arr;

	if (!changelists.IsEmpty())
	{
		for (int nItem = 0; nItem < changelists.GetCount(); nItem++)
		{
			const char * c = apr_pstrdup(pool, (LPCSTR)CUnicodeUtils::GetUTF8(changelists[nItem]));
			(*((const char **) apr_array_push(arr))) = c;
		}
	}
	return arr;
}

apr_hash_t * SVN::MakeRevPropHash(const RevPropHash revProps, apr_pool_t * pool)
{
	apr_hash_t * revprop_table = NULL;
	if (revProps.size())
	{
		revprop_table = apr_hash_make(pool);
		for (RevPropHash::const_iterator it = revProps.begin(); it != revProps.end(); ++it)
		{
			svn_string_t *propval = svn_string_create((LPCSTR)CUnicodeUtils::GetUTF8(it->second), pool);
			apr_hash_set (revprop_table, apr_pstrdup(pool, (LPCSTR)CUnicodeUtils::GetUTF8(it->first)), APR_HASH_KEY_STRING, (const void*)propval);
		}
	}

	return revprop_table;
}

void SVN::SetAndClearProgressInfo(HWND hWnd)
{
	m_progressWnd = hWnd;
	m_pProgressDlg = NULL;
	progress_total = 0;
	progress_lastprogress = 0;
	progress_lasttotal = 0;
	progress_lastTicks = GetTickCount();
}

void SVN::SetAndClearProgressInfo(CProgressDlg * pProgressDlg, bool bShowProgressBar/* = false*/)
{
	m_progressWnd = NULL;
	m_pProgressDlg = pProgressDlg;
	progress_total = 0;
	progress_lastprogress = 0;
	progress_lasttotal = 0;
	progress_lastTicks = GetTickCount();
	m_bShowProgressBar = bShowProgressBar;
}

CString SVN::GetSummarizeActionText(svn_client_diff_summarize_kind_t kind)
{
	CString sAction;
	switch (kind)
	{
	case svn_client_diff_summarize_kind_normal:
		sAction.LoadString(IDS_SVN_SUMMARIZENORMAL);
		break;
	case svn_client_diff_summarize_kind_added:
		sAction.LoadString(IDS_SVN_SUMMARIZEADDED);
		break;
	case svn_client_diff_summarize_kind_modified:
		sAction.LoadString(IDS_SVN_SUMMARIZEMODIFIED);
		break;
	case svn_client_diff_summarize_kind_deleted:
		sAction.LoadString(IDS_SVN_SUMMARIZEDELETED);
		break;
	}
	return sAction;
}

void SVN::progress_func(apr_off_t progress, apr_off_t total, void *baton, apr_pool_t * /*pool*/)
{
	SVN * pSVN = (SVN*)baton;
	if ((pSVN==0)||((pSVN->m_progressWnd == 0)&&(pSVN->m_pProgressDlg == 0)))
		return;
	apr_off_t delta = progress;
	if ((progress >= pSVN->progress_lastprogress)&&(total == pSVN->progress_lasttotal))
		delta = progress - pSVN->progress_lastprogress;
	// because of http://subversion.tigris.org/issues/show_bug.cgi?id=3260
	// the progress information can be horribly wrong.
	// We cut the delta here to 8kb because SVN does not send/receive packets
	// bigger than this, and we can therefore reduce the error that way a little bit
	if (delta > 8192)
	{
		delta = delta % 8192;
	}

	pSVN->progress_lastprogress = progress;
	pSVN->progress_lasttotal = total;
	
	DWORD ticks = GetTickCount();
	pSVN->progress_vector.push_back(delta);
	pSVN->progress_total += delta;
	//ATLTRACE("progress = %I64d, total = %I64d, delta = %I64d, overall total is : %I64d\n", progress, total, delta, pSVN->progress_total);
	if ((pSVN->progress_lastTicks + 1000) < ticks)
	{
		double divby = (double(ticks - pSVN->progress_lastTicks)/1000.0);
		if (divby == 0)
			divby = 1;
		pSVN->m_SVNProgressMSG.overall_total = pSVN->progress_total;
		pSVN->m_SVNProgressMSG.progress = progress;
		pSVN->m_SVNProgressMSG.total = total;
		pSVN->progress_lastTicks = ticks;
		apr_off_t average = 0;
		for (std::vector<apr_off_t>::iterator it = pSVN->progress_vector.begin(); it != pSVN->progress_vector.end(); ++it)
		{
			average += *it;
		}
		average = apr_off_t(double(average) / divby);
		pSVN->m_SVNProgressMSG.BytesPerSecond = average;
		if (average < 1024)
			pSVN->m_SVNProgressMSG.SpeedString.Format(IDS_SVN_PROGRESS_BYTES_SEC, average);
		else
		{
			double averagekb = (double)average / 1024.0;
			pSVN->m_SVNProgressMSG.SpeedString.Format(IDS_SVN_PROGRESS_KBYTES_SEC, averagekb);
		}
		if (pSVN->m_progressWnd)
			SendMessage(pSVN->m_progressWnd, WM_SVNPROGRESS, 0, (LPARAM)&pSVN->m_SVNProgressMSG);
		else if (pSVN->m_pProgressDlg)
		{
			if ((pSVN->m_bShowProgressBar && (progress > 1000) && (total > 0)))
				pSVN->m_pProgressDlg->SetProgress64(progress, total);

			CString sTotal;
			CString temp;
			if (pSVN->m_SVNProgressMSG.overall_total < 1024)
				sTotal.Format(IDS_SVN_PROGRESS_TOTALBYTESTRANSFERRED, pSVN->m_SVNProgressMSG.overall_total);
			else if (pSVN->m_SVNProgressMSG.overall_total < 1200000)
				sTotal.Format(IDS_SVN_PROGRESS_TOTALTRANSFERRED, pSVN->m_SVNProgressMSG.overall_total / 1024);
			else
				sTotal.Format(IDS_SVN_PROGRESS_TOTALMBTRANSFERRED, (double)((double)pSVN->m_SVNProgressMSG.overall_total / 1024000.0));
			temp.Format(IDS_SVN_PROGRESS_TOTALANDSPEED, (LPCTSTR)sTotal, (LPCTSTR)pSVN->m_SVNProgressMSG.SpeedString);

			pSVN->m_pProgressDlg->SetLine(2, temp);
		}
		pSVN->progress_vector.clear();
	}
	return;
}

svn_error_t * svn_error_handle_malfunction(svn_boolean_t can_return,
										   const char *file, int line,
										   const char *expr)
{
	// we get here every time Subversion encounters something very unexpected.
	// in previous versions, Subversion would just call abort() - now we can
	// show the user some information before we return.
	svn_error_t * err = svn_error_raise_on_malfunction(TRUE, file, line, expr);

	CString sErr(MAKEINTRESOURCE(IDS_ERR_SVNEXCEPTION));
	if (err)
	{
		sErr += _T("\n\n") + SVN::GetErrorString(err);
		::MessageBox(NULL, sErr, _T("Subversion Exception!"), MB_ICONERROR);
		if (can_return)
			return err;
		if (CRegDWORD(_T("Software\\TortoiseSVN\\Debug"), FALSE)==FALSE)
			abort();	// ugly, ugly! But at least we showed a messagebox first
	}

	CString sFormatErr;
	sFormatErr.Format(IDS_ERR_SVNFORMATEXCEPTION, CUnicodeUtils::GetUnicode(file), line, CUnicodeUtils::GetUnicode(expr));
	::MessageBox(NULL, sFormatErr, _T("Subversion Exception!"), MB_ICONERROR);
	if (CRegDWORD(_T("Software\\TortoiseSVN\\Debug"), FALSE)==FALSE)
		abort();	// ugly, ugly! But at least we showed a messagebox first
	return NULL;	// never reached, only to silence compiler warning
}

