#include "util/Logger.hpp"
#include "BasicImporter.hpp"

static fail::Logger LOG("BasicImporter");

bool BasicImporter::create_database() {
	std::string create_statement = "CREATE TABLE IF NOT EXISTS trace ("
		"	variant_id int(11) NOT NULL,"
		"	instr1 int(10) unsigned NOT NULL,"
		"	instr1_absolute int(10) unsigned DEFAULT NULL,"
		"	instr2 int(10) unsigned NOT NULL,"
		"	instr2_absolute int(10) unsigned DEFAULT NULL,"
		"	time1 bigint(10) unsigned NOT NULL,"
		"	time2 bigint(10) unsigned NOT NULL,"
		"	data_address int(10) unsigned NOT NULL,"
		"	width tinyint(3) unsigned NOT NULL,"
		"	accesstype enum('R','W') NOT NULL,"
		"	PRIMARY KEY (variant_id,instr2,data_address)"
		") engine=MyISAM ";
	return db->query(create_statement.c_str());
}

bool BasicImporter::add_trace_event(margin_info_t &begin, margin_info_t &end,
									const Trace_Event &event, bool is_fake) {
	static MYSQL_STMT *stmt = 0;
	if (!stmt) {
		std::string sql("INSERT INTO trace (variant_id, instr1, instr1_absolute, instr2, instr2_absolute, time1, time2, data_address, width,"
		                "					accesstype)"
		                "VALUES (?,?,?,?,?,?,?,?,?,?)");
		stmt = mysql_stmt_init(db->getHandle());
		if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length())) {
			LOG << "query '" << sql << "' failed: " << mysql_error(db->getHandle()) << std::endl;
			return false;
		}
	}

	MYSQL_BIND bind[10];
	my_bool is_null = is_fake;
	my_bool null = true;

	unsigned long accesstype_len = 1;

	unsigned data_address = event.memaddr();
	unsigned width = event.width();

	char accesstype = event.accesstype() == event.READ ? 'R' : 'W';
	// LOG << m_variant_id  << "-" << ":" << begin.dyninstr << ":" << end.dyninstr << "-" << data_address << " " << accesstype << std::endl;


	memset(bind, 0, sizeof(bind));
	for (unsigned i = 0; i < sizeof(bind)/sizeof(*bind); ++i) {
		bind[i].buffer_type = MYSQL_TYPE_LONG;
		bind[i].is_unsigned = 1;
		switch (i) {
		case 0: bind[i].buffer = &m_variant_id; break;
		case 1: bind[i].buffer = &begin.dyninstr; break;
		case 2: bind[i].buffer = &begin.ip;
			bind[i].is_null = begin.ip == 0 ? &null : &is_null;
			break;
		case 3: bind[i].buffer = &end.dyninstr; break;
		case 4: bind[i].buffer = &end.ip;
			bind[i].is_null = &is_null; break;
		case 5: bind[i].buffer = &begin.time;
			bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
			break;
		case 6: bind[i].buffer = &end.time;
			bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
			break;
		case 7: bind[i].buffer = &data_address; break;
		case 8: bind[i].buffer = &width; break;
		case 9: bind[i].buffer = &accesstype;
			bind[i].buffer_type = MYSQL_TYPE_STRING;
			bind[i].buffer_length = accesstype_len;
			bind[i].length = &accesstype_len;
			break;
		}
	}
	if (mysql_stmt_bind_param(stmt, bind)) {
		LOG << "mysql_stmt_bind_param() failed: " << mysql_stmt_error(stmt) << std::endl;
		return false;
	}
	if (mysql_stmt_execute(stmt)) {
		LOG << "mysql_stmt_execute() failed: " << mysql_stmt_error(stmt) << std::endl;
		LOG << "IP: " << std::hex << event.ip() << std::endl;
		return false;
	}
	return true;
}
