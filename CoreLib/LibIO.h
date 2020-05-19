#ifndef CORE_LIB_IO_H
#define CORE_LIB_IO_H

#include "LibString.h"
#include "Stream.h"
#include "TextIO.h"
#include "SecureCRT.h"

namespace CoreLib
{
	namespace IO
	{
		class File
		{
		public:
			static bool Exists(const CoreLib::Basic::String & fileName);
			static CoreLib::Basic::String ReadAllText(const CoreLib::Basic::String & fileName);
			static void WriteAllText(const CoreLib::Basic::String & fileName, const CoreLib::Basic::String & text);
			static CoreLib::Basic::List<unsigned char> ReadAllBytes(const CoreLib::Basic::String & fileName);
            static void WriteAllBytes(const CoreLib::Basic::String & fileName, void * buffer, size_t size);
		};

		enum class DirectoryEntryType
		{
			Unknown,
			File,
			Directory
		};

		class DirectoryEntry
		{
		public:
			CoreLib::String name;
			CoreLib::String fullPath;
			DirectoryEntryType type;
		};

		struct DirectoryIteratorContext;

		class DirectoryIterator
		{
		public:
			CoreLib::ObjPtr<DirectoryIteratorContext> context;
		public:
			DirectoryIterator();
			DirectoryIterator(const DirectoryIterator& other);
			DirectoryIterator(DirectoryIterator &&other);

			DirectoryIterator(String path);
			~DirectoryIterator();

			DirectoryIterator begin()
			{
				return *this;
			}
			DirectoryIterator end()
			{
				return DirectoryIterator();
			}
			DirectoryEntry operator*();
			DirectoryIterator& operator++();
			bool operator==(const DirectoryIterator& other);
			bool operator!=(const DirectoryIterator& other)
			{
				return !(*this == other);
			}
		};

		class Path
		{
		public:
#ifdef _WIN32
			static const char PathDelimiter = '\\';
			static const char AltPathDelimiter = '/';

#else
			static const char PathDelimiter = '/';
			static const char AltPathDelimiter = '\\';

#endif
			static String TruncateExt(const String & path);
			static String ReplaceExt(const String & path, const char * newExt);
			static String GetFileName(const String & path);
			static String GetFileNameWithoutEXT(const String & path);
			static String GetFileExt(const String & path);
			static String GetDirectoryName(const String & path);
			static String Combine(const String & path1, const String & path2);
			static String Combine(const String & path1, const String & path2, const String & path3);
			static bool CreateDir(const String & path);
			static List<String> Split(String path);
			static String Normalize(String path);
			static bool IsSubPathOf(String path, String parentPath);
			static String GetRelativePath(String path, String referencePath);
			static bool IsDirectory(String path);
			static bool IsAbsolute(String path);
		};

		class CommandLineWriter : public Object
		{
		public:
			virtual void Write(const String & text) = 0;
		};

		void SetCommandLineWriter(CommandLineWriter * writer);

		extern CommandLineWriter * currentCommandWriter;
		template<typename ...Args>
		void uiprintf(const wchar_t * format, Args... args)
		{
			if (currentCommandWriter)
			{
				char buffer[1024];
				snprintf(buffer, 1024, format, args...);
				currentCommandWriter->Write(buffer);
			}
		}
	}
}

#endif