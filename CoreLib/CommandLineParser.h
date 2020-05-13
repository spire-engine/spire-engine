#ifndef CORE_LIB_COMMANDLINE_PARSER
#define CORE_LIB_COMMANDLINE_PARSER

#include "Tokenizer.h"

namespace CoreLib
{
	namespace Text
	{
		class CommandLineParser : public Object
		{
		private:
			List<String> stream;
		public:
			CommandLineParser() = default;
			CommandLineParser(const String & cmdLine);
			void Parse(const String & cmdLine);
			void SetArguments(int argc, const char ** argv);
			String GetFileName();
			bool OptionExists(const String & opt);
			String GetOptionValue(const String & opt);
			String GetToken(int id);
			int GetTokenCount();
		};
	}
}

#endif