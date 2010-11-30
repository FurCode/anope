/* ChanServ core functions
 *
 * (C) 2003-2010 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 */

/*************************************************************************/

#include "module.h"

class AccessListCallback : public NumberList
{
 protected:
	CommandSource &source;
	bool SentHeader;
 public:
	AccessListCallback(CommandSource &_source, const Anope::string &numlist) : NumberList(numlist, false), source(_source), SentHeader(false)
	{
	}

	~AccessListCallback()
	{
		if (SentHeader)
			source.Reply(CHAN_ACCESS_LIST_FOOTER, source.ci->name.c_str());
		else
			source.Reply(CHAN_ACCESS_NO_MATCH, source.ci->name.c_str());
	}

	virtual void HandleNumber(unsigned Number)
	{
		if (!Number || Number > source.ci->GetAccessCount())
			return;

		if (!SentHeader)
		{
			SentHeader = true;
			source.Reply(CHAN_ACCESS_LIST_HEADER, source.ci->name.c_str());
		}

		DoList(source, Number - 1, source.ci->GetAccess(Number - 1));
	}

	static void DoList(CommandSource &source, unsigned Number, ChanAccess *access)
	{
		if (source.ci->HasFlag(CI_XOP))
		{
			Anope::string xop = get_xop_level(access->level);
			source.Reply(CHAN_ACCESS_LIST_XOP_FORMAT, Number + 1, xop.c_str(), access->mask.c_str());
		}
		else
			source.Reply(CHAN_ACCESS_LIST_AXS_FORMAT, Number + 1, access->level, access->mask.c_str());
	}
};

class AccessViewCallback : public AccessListCallback
{
 public:
	AccessViewCallback(CommandSource &_source, const Anope::string &numlist) : AccessListCallback(_source, numlist)
	{
	}

	void HandleNumber(unsigned Number)
	{
		if (!Number || Number > source.ci->GetAccessCount())
			return;

		if (!SentHeader)
		{
			SentHeader = true;
			source.Reply(CHAN_ACCESS_LIST_HEADER, source.ci->name.c_str());
		}

		DoList(source, Number - 1, source.ci->GetAccess(Number - 1));
	}

	static void DoList(CommandSource &source, unsigned Number, ChanAccess *access)
	{
		User *u = source.u;
		ChannelInfo *ci = source.ci;
		Anope::string timebuf;
		if (ci->c && nc_on_chan(ci->c, access->nc))
			timebuf = "Now";
		else if (access->last_seen == 0)
			timebuf = "Never";
		else
			timebuf = do_strftime(access->last_seen);

		if (ci->HasFlag(CI_XOP))
		{
			Anope::string xop = get_xop_level(access->level);
			source.Reply(CHAN_ACCESS_VIEW_XOP_FORMAT, Number + 1, xop.c_str(), access->mask.c_str(), access->creator.c_str(), timebuf.c_str());
		}
		else
			source.Reply(CHAN_ACCESS_VIEW_AXS_FORMAT, Number + 1, access->level, access->mask.c_str(), access->creator.c_str(), timebuf.c_str());
	}
};

class AccessDelCallback : public NumberList
{
	CommandSource &source;
	Command *c;
	unsigned Deleted;
	Anope::string Nicks;
	bool Denied;
	bool override;
 public:
	AccessDelCallback(CommandSource &_source, Command *_c, const Anope::string &numlist) : NumberList(numlist, true), source(_source), c(_c), Deleted(0), Denied(false)
	{
		if (!check_access(source.u, source.ci, CA_ACCESS_CHANGE) && source.u->Account()->HasPriv("chanserv/access/modify"))
			this->override = true;
	}

	~AccessDelCallback()
	{
		if (Denied && !Deleted)
			source.Reply(ACCESS_DENIED);
		else if (!Deleted)
			source.Reply(CHAN_ACCESS_NO_MATCH, source.ci->name.c_str());
		else
		{
			Log(override ? LOG_OVERRIDE : LOG_COMMAND, source.u, c, source.ci) << "for user" << (Deleted == 1 ? " " : "s ") << Nicks;

			if (Deleted == 1)
				source.Reply(CHAN_ACCESS_DELETED_ONE, source.ci->name.c_str());
			else
				source.Reply(CHAN_ACCESS_DELETED_SEVERAL, Deleted, source.ci->name.c_str());
		}
	}

	void HandleNumber(unsigned Number)
	{
		if (!Number || Number > source.ci->GetAccessCount())
			return;

		User *u = source.u;
		ChannelInfo *ci = source.ci;

		ChanAccess *access = ci->GetAccess(Number - 1);

		ChanAccess *u_access = ci->GetAccess(u);
		int16 u_level = u_access ? u_access->level : 0;
		if (u_level <= access->level && !u->Account()->HasPriv("chanserv/access/modify"))
		{
			Denied = true;
			return;
		}

		++Deleted;
		if (!Nicks.empty())
			Nicks += ", " + access->mask;
		else
			Nicks = access->mask;

		FOREACH_MOD(I_OnAccessDel, OnAccessDel(ci, u, access));

		ci->EraseAccess(Number - 1);
	}
};

class CommandCSAccess : public Command
{
	CommandReturn DoAdd(CommandSource &source, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		ChannelInfo *ci = source.ci;

		Anope::string mask = params[2];
		int level = params[3].is_number_only() ? convertTo<int>(params[3]) : ACCESS_INVALID;

		ChanAccess *u_access = ci->GetAccess(u);
		int16 u_level = u_access ? u_access->level : 0;
		if (level >= u_level && !u->Account()->HasPriv("chanserv/access/modify"))
		{
			source.Reply(ACCESS_DENIED);
			return MOD_CONT;
		}

		if (!level)
		{
			source.Reply(CHAN_ACCESS_LEVEL_NONZERO);
			return MOD_CONT;
		}
		else if (level <= ACCESS_INVALID || level >= ACCESS_FOUNDER)
		{
			source.Reply(CHAN_ACCESS_LEVEL_RANGE, ACCESS_INVALID + 1, ACCESS_FOUNDER - 1);
			return MOD_CONT;
		}

		bool override = !check_access(u, ci, CA_ACCESS_CHANGE) || level >= u_level;

		NickAlias *na = findnick(mask);
		if (!na && mask.find_first_of("!@*") == Anope::string::npos)
			mask += "!*@*";
		else if (na && na->HasFlag(NS_FORBIDDEN))
		{
			source.Reply(NICK_X_FORBIDDEN, mask.c_str());
			return MOD_CONT;
		}

		ChanAccess *access = ci->GetAccess(mask);
		if (access)
		{
			/* Don't allow lowering from a level >= u_level */
			if (access->level >= u_level && !u->Account()->HasPriv("chanserv/access/modify"))
			{
				source.Reply(ACCESS_DENIED);
				return MOD_CONT;
			}
			if (access->level == level)
			{
				source.Reply(CHAN_ACCESS_LEVEL_UNCHANGED, access->mask.c_str(), ci->name.c_str(), level);
				return MOD_CONT;
			}
			access->level = level;

			FOREACH_MOD(I_OnAccessChange, OnAccessChange(ci, u, access));

			Log(override ? LOG_OVERRIDE : LOG_COMMAND, u, this, ci) << "ADD " << na->nick << " (level: " << level << ") as level " << u_level;
			source.Reply(CHAN_ACCESS_LEVEL_CHANGED, access->mask.c_str(), ci->name.c_str(), level);
			return MOD_CONT;
		}

		if (ci->GetAccessCount() >= Config->CSAccessMax)
		{
			source.Reply(CHAN_ACCESS_REACHED_LIMIT, Config->CSAccessMax);
			return MOD_CONT;
		}

		access = ci->AddAccess(mask, level, u->nick);

		FOREACH_MOD(I_OnAccessAdd, OnAccessAdd(ci, u, access));

		Log(override ? LOG_OVERRIDE : LOG_COMMAND, u, this, ci) << "ADD " << mask << " (level: " << level << ") as level " << u_level;
		source.Reply(CHAN_ACCESS_ADDED, access->mask.c_str(), ci->name.c_str(), level);

		return MOD_CONT;
	}

	CommandReturn DoDel(CommandSource &source, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		ChannelInfo *ci = source.ci;

		const Anope::string &mask = params[2];

		if (!ci->GetAccessCount())
			source.Reply(CHAN_ACCESS_LIST_EMPTY, ci->name.c_str());
		else if (isdigit(mask[0]) && mask.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			AccessDelCallback list(source, this, mask);
			list.Process();
		}
		else
		{
			ChanAccess *access = ci->GetAccess(mask);
			ChanAccess *u_access = ci->GetAccess(u);
			int16 u_level = u_access ? u_access->level : 0;
			if (!access)
				source.Reply(CHAN_ACCESS_NOT_FOUND, mask.c_str(), ci->name.c_str());
			else if (access->nc != u->Account() && check_access(u, ci, CA_NOJOIN) && u_level <= access->level && !u->Account()->HasPriv("chanserv/access/modify"))
				source.Reply(ACCESS_DENIED);
			else
			{
				source.Reply(CHAN_ACCESS_DELETED, access->mask.c_str(), ci->name.c_str());
				bool override = !check_access(u, ci, CA_ACCESS_CHANGE) && access->nc != u->Account();
				Log(override ? LOG_OVERRIDE : LOG_COMMAND, u, this, ci) << "DEL " << access->mask << " from level " << access->level;

				FOREACH_MOD(I_OnAccessDel, OnAccessDel(ci, u, access));

				ci->EraseAccess(access);
			}
		}

		return MOD_CONT;
	}

	CommandReturn DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		ChannelInfo *ci = source.ci;

		const Anope::string &nick = params.size() > 2 ? params[2] : "";

		if (!ci->GetAccessCount())
			source.Reply(CHAN_ACCESS_LIST_EMPTY, ci->name.c_str());
		else if (!nick.empty() && nick.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			AccessListCallback list(source, nick);
			list.Process();
		}
		else
		{
			bool SentHeader = false;

			for (unsigned i = 0, end = ci->GetAccessCount(); i < end; ++i)
			{
				ChanAccess *access = ci->GetAccess(i);

				if (!nick.empty() && !Anope::Match(access->mask, nick))
					continue;

				if (!SentHeader)
				{
					SentHeader = true;
					source.Reply(CHAN_ACCESS_LIST_HEADER, ci->name.c_str());
				}

				AccessListCallback::DoList(source, i, access);
			}

			if (SentHeader)
				source.Reply(CHAN_ACCESS_LIST_FOOTER, ci->name.c_str());
			else
				source.Reply(CHAN_ACCESS_NO_MATCH, ci->name.c_str());
		}

		return MOD_CONT;
	}

	CommandReturn DoView(CommandSource &source, const std::vector<Anope::string> &params)
	{
		ChannelInfo *ci = source.ci;

		const Anope::string &nick = params.size() > 2 ? params[2] : "";

		if (!ci->GetAccessCount())
			source.Reply(CHAN_ACCESS_LIST_EMPTY, ci->name.c_str());
		else if (!nick.empty() && nick.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			AccessViewCallback list(source, nick);
			list.Process();
		}
		else
		{
			bool SentHeader = false;

			for (unsigned i = 0, end = ci->GetAccessCount(); i < end; ++i)
			{
				ChanAccess *access = ci->GetAccess(i);

				if (!nick.empty() && !Anope::Match(access->mask, nick))
					continue;

				if (!SentHeader)
				{
					SentHeader = true;
					source.Reply(CHAN_ACCESS_LIST_HEADER, ci->name.c_str());
				}

				AccessViewCallback::DoList(source, i, access);
			}

			if (SentHeader)
				source.Reply(CHAN_ACCESS_LIST_FOOTER, ci->name.c_str());
			else
				source.Reply(CHAN_ACCESS_NO_MATCH, ci->name.c_str());
		}

		return MOD_CONT;
	}

	CommandReturn DoClear(CommandSource &source)
	{
		User *u = source.u;
		ChannelInfo *ci = source.ci;

		if (!IsFounder(u, ci) && !u->Account()->HasPriv("chanserv/access/modify"))
			source.Reply(ACCESS_DENIED);
		else
		{
			ci->ClearAccess();

			FOREACH_MOD(I_OnAccessClear, OnAccessClear(ci, u));

			source.Reply(CHAN_ACCESS_CLEAR, ci->name.c_str());

			bool override = !IsFounder(u, ci);
			Log(override ? LOG_OVERRIDE : LOG_COMMAND, u, this, ci) << "CLEAR";
		}

		return MOD_CONT;
	}

 public:
	CommandCSAccess() : Command("ACCESS", 2, 4)
	{
	}

	CommandReturn Execute(CommandSource &source, const std::vector<Anope::string> &params)
	{
		const Anope::string &cmd = params[1];
		const Anope::string &nick = params.size() > 2 ? params[2] : "";
		const Anope::string &s = params.size() > 3 ? params[3] : "";

		User *u = source.u;
		ChannelInfo *ci = source.ci;

		bool is_list = cmd.equals_ci("LIST") || cmd.equals_ci("VIEW");
		bool is_clear = cmd.equals_ci("CLEAR");
		bool is_del = cmd.equals_ci("DEL");

		bool has_access = false;
		if (is_list && check_access(u, ci, CA_ACCESS_LIST))
			has_access = true;
		else if (check_access(u, ci, CA_ACCESS_CHANGE))
			has_access = true;
		else if (is_del)
		{
			NickAlias *na = findnick(nick);
			if (na && na->nc == u->Account())
				has_access = true;
		}

		/* If LIST, we don't *require* any parameters, but we can take any.
		 * If DEL, we require a nick and no level.
		 * Else (ADD), we require a level (which implies a nick). */
		if (is_list || is_clear ? 0 : (cmd.equals_ci("DEL") ? (nick.empty() || !s.empty()) : s.empty()))
			this->OnSyntaxError(source, cmd);
		else if (!has_access)
			source.Reply(ACCESS_DENIED);
		/* We still allow LIST and CLEAR in xOP mode, but not others */
		else if (ci->HasFlag(CI_XOP) && !is_list && !is_clear)
		{
			if (ModeManager::FindChannelModeByName(CMODE_HALFOP))
				source.Reply(CHAN_ACCESS_XOP_HOP, Config->s_ChanServ.c_str());
			else
				source.Reply(CHAN_ACCESS_XOP, Config->s_ChanServ.c_str());
		}
		else if (readonly && !is_list)
			source.Reply(CHAN_ACCESS_DISABLED);
		else if (cmd.equals_ci("ADD"))
			this->DoAdd(source, params);
		else if (cmd.equals_ci("DEL"))
			this->DoDel(source, params);
		else if (cmd.equals_ci("LIST"))
			this->DoList(source, params);
		else if (cmd.equals_ci("VIEW"))
			this->DoView(source, params);
		else if (cmd.equals_ci("CLEAR"))
			this->DoClear(source);
		else
			this->OnSyntaxError(source, "");

		return MOD_CONT;
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand)
	{
		source.Reply(CHAN_HELP_ACCESS);
		source.Reply(CHAN_HELP_ACCESS_LEVELS);
		return true;
	}

	void OnSyntaxError(CommandSource &source, const Anope::string &subcommand)
	{
		SyntaxError(source, "ACCESS", CHAN_ACCESS_SYNTAX);
	}
};

class CommandCSLevels : public Command
{
	CommandReturn DoSet(CommandSource &source, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		ChannelInfo *ci = source.ci;

		const Anope::string &what = params[2];
		const Anope::string &lev = params[3];

		Anope::string error;
		int level = (lev.is_number_only() ? convertTo<int>(lev, error, false) : 0);
		if (!lev.is_number_only())
			error = "1";

		if (lev.equals_ci("FOUNDER"))
		{
			level = ACCESS_FOUNDER;
			error.clear();
		}

		if (!error.empty())
			this->OnSyntaxError(source, "SET");
		else if (level <= ACCESS_INVALID || level > ACCESS_FOUNDER)
			source.Reply(CHAN_LEVELS_RANGE, ACCESS_INVALID + 1, ACCESS_FOUNDER - 1);
		else
		{
			for (int i = 0; levelinfo[i].what >= 0; ++i)
			{
				if (what.equals_ci(levelinfo[i].name))
				{
					ci->levels[levelinfo[i].what] = level;
					FOREACH_MOD(I_OnLevelChange, OnLevelChange(u, ci, i, level));

					bool override = !check_access(u, ci, CA_FOUNDER);
					Log(override ? LOG_OVERRIDE : LOG_COMMAND, u, this, ci) << "SET " << levelinfo[i].name << " to " << level;

					if (level == ACCESS_FOUNDER)
						source.Reply(CHAN_LEVELS_CHANGED_FOUNDER, levelinfo[i].name.c_str(), ci->name.c_str());
					else
						source.Reply(CHAN_LEVELS_CHANGED, levelinfo[i].name.c_str(), ci->name.c_str(), level);
					return MOD_CONT;
				}
			}

			source.Reply(CHAN_LEVELS_UNKNOWN, what.c_str(), Config->s_ChanServ.c_str());
		}

		return MOD_CONT;
	}

	CommandReturn DoDisable(CommandSource &source, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		ChannelInfo *ci = source.ci;

		const Anope::string &what = params[2];

		/* Don't allow disabling of the founder level. It would be hard to change it back if you dont have access to use this command */
		if (what.equals_ci("FOUNDER"))
			for (int i = 0; levelinfo[i].what >= 0; ++i)
			{
				if (what.equals_ci(levelinfo[i].name))
				{
					ci->levels[levelinfo[i].what] = ACCESS_INVALID;
					FOREACH_MOD(I_OnLevelChange, OnLevelChange(u, ci, i, levelinfo[i].what));

					bool override = !check_access(u, ci, CA_FOUNDER);
					Log(override ? LOG_OVERRIDE : LOG_COMMAND, u, this, ci) << "DISABLE " << levelinfo[i].name;

					source.Reply(CHAN_LEVELS_DISABLED, levelinfo[i].name.c_str(), ci->name.c_str());
					return MOD_CONT;
				}
			}

		source.Reply(CHAN_LEVELS_UNKNOWN, what.c_str(), Config->s_ChanServ.c_str());

		return MOD_CONT;
	}

	CommandReturn DoList(CommandSource &source)
	{
		ChannelInfo *ci = source.ci;

		source.Reply(CHAN_LEVELS_LIST_HEADER, ci->name.c_str());

		if (!levelinfo_maxwidth)
			for (int i = 0; levelinfo[i].what >= 0; ++i)
			{
				int len = levelinfo[i].name.length();
				if (len > levelinfo_maxwidth)
					levelinfo_maxwidth = len;
			}

		for (int i = 0; levelinfo[i].what >= 0; ++i)
		{
			int j = ci->levels[levelinfo[i].what];

			if (j == ACCESS_INVALID)
			{
				j = levelinfo[i].what;

				source.Reply(CHAN_LEVELS_LIST_DISABLED, levelinfo_maxwidth, levelinfo[i].name.c_str());
			}
			else if (j == ACCESS_FOUNDER)
				source.Reply(CHAN_LEVELS_LIST_FOUNDER, levelinfo_maxwidth, levelinfo[i].name.c_str());
			else
				source.Reply(CHAN_LEVELS_LIST_NORMAL, levelinfo_maxwidth, levelinfo[i].name.c_str(), j);
		}

		return MOD_CONT;
	}

	CommandReturn DoReset(CommandSource &source)
	{
		User *u = source.u;
		ChannelInfo *ci = source.ci;

		reset_levels(ci);
		FOREACH_MOD(I_OnLevelChange, OnLevelChange(u, ci, -1, 0));

		bool override = !check_access(u, ci, CA_FOUNDER);
		Log(override ? LOG_OVERRIDE : LOG_COMMAND, u, this, ci) << "RESET";

		source.Reply(CHAN_LEVELS_RESET, ci->name.c_str());
		return MOD_CONT;
	}

 public:
	CommandCSLevels() : Command("LEVELS", 2, 4)
	{
	}

	CommandReturn Execute(CommandSource &source, const std::vector<Anope::string> &params)
	{
		const Anope::string &cmd = params[1];
		const Anope::string &what = params.size() > 2 ? params[2] : "";
		const Anope::string &s = params.size() > 3 ? params[3] : "";

		User *u = source.u;
		ChannelInfo *ci = source.ci;

		/* If SET, we want two extra parameters; if DIS[ABLE] or FOUNDER, we want only
		 * one; else, we want none.
		 */
		if (cmd.equals_ci("SET") ? s.empty() : (cmd.substr(0, 3).equals_ci("DIS") ? (what.empty() || !s.empty()) : !what.empty()))
			this->OnSyntaxError(source, cmd);
		else if (ci->HasFlag(CI_XOP))
			source.Reply(CHAN_LEVELS_XOP);
		else if (!check_access(u, ci, CA_FOUNDER) && !u->Account()->HasPriv("chanserv/access/modify"))
			source.Reply(ACCESS_DENIED);
		else if (cmd.equals_ci("SET"))
			this->DoSet(source, params);
		else if (cmd.equals_ci("DIS") || cmd.equals_ci("DISABLE"))
			this->DoDisable(source, params);
		else if (cmd.equals_ci("LIST"))
			this->DoList(source);
		else if (cmd.equals_ci("RESET"))
			this->DoReset(source);
		else
			this->OnSyntaxError(source, "");

		return MOD_CONT;
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand)
	{
		if (subcommand.equals_ci("DESC"))
		{
			int i;
			source.Reply(CHAN_HELP_LEVELS_DESC);
			if (!levelinfo_maxwidth)
				for (i = 0; levelinfo[i].what >= 0; ++i)
				{
					int len = levelinfo[i].name.length();
					if (len > levelinfo_maxwidth)
						levelinfo_maxwidth = len;
				}
			for (i = 0; levelinfo[i].what >= 0; ++i)
				source.Reply(CHAN_HELP_LEVELS_DESC_FORMAT, levelinfo_maxwidth, levelinfo[i].name.c_str(), GetString(source.u, levelinfo[i].desc).c_str());
		}
		else
			source.Reply(CHAN_HELP_LEVELS);
		return true;
	}

	void OnSyntaxError(CommandSource &source, const Anope::string &subcommand)
	{
		SyntaxError(source, "LEVELS", CHAN_LEVELS_SYNTAX);
	}

	void OnServHelp(CommandSource &source)
	{
		source.Reply(CHAN_HELP_CMD_ACCESS);
		source.Reply(CHAN_HELP_CMD_LEVELS);
	}
};

class CSAccess : public Module
{
	CommandCSAccess commandcsaccess;
	CommandCSLevels commandcslevels;

 public:
	CSAccess(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator)
	{
		this->SetAuthor("Anope");
		this->SetType(CORE);

		this->AddCommand(ChanServ, &commandcsaccess);
		this->AddCommand(ChanServ, &commandcslevels);
	}
};

MODULE_INIT(CSAccess)