/// \file
/// \brief Fst file module
/// \author SpinelDusk
/// \details Fst file processing class module

#pragma GCC diagnostic ignored "-Wc++98-compat"					//Disable warning: 'nullptr' is incompatible with C++98
#pragma GCC diagnostic ignored "-Wdisabled-macro-expansion"		//Disable warning: Disabled expansion of recursive macro
#pragma GCC diagnostic ignored "-Wlanguage-extension-token"		//Disable warning: Extension used

#pragma package(smart_init)//Ensures that packaged units are initialized in the order determined by their dependencies

#include "FstFile.h"

extern ErrorData LastError;///< Defined in ErrorDescription.cpp
/// \brief Macro makes it easy to write SendAlert function in the code
#define SendAlert(a,b) SendAlert(__FILE__,__FUNCSIG__,__LINE__,a,3,b)

/// \brief Clearing instance fields (pack instance)
void FstPack::Clear()
{
	AmountEntries = 0;
	FileSize = 0;
	ArchivePath.erase(ArchivePath.begin(), ArchivePath.end());
	ListPath.erase(ListPath.begin(), ListPath.end());
	if(OpenList.is_open())
		OpenList.close();
	if(OpenReceiver.is_open())
		OpenReceiver.close();
	TOCList.erase(TOCList.begin(), TOCList.end());
}
/// \brief Fst-file packing control function
/// \param[in] local_Input Path to the list of files to be packed
/// \param[in] local_Output Path to the created archive
/// \return Error code: 0 - no errors, 1 - operation error
int FstPack::Pack(char* local_Input, char* local_Output)
{
	//Clearing the instance fields
	Clear();

	//Trying to open list-file
	OpenList.open(local_Input, ios::in);
	if(!OpenList.is_open())
	{
		OpenList.close();
		SendAlert(1, local_Input);//Can't open input file list.
		return 1;
	}

	//Get the file size
	FileSize = chf::GetFileSize(OpenList);
	if(FileSize == 0)
	{
		OpenList.close();
		SendAlert(2, local_Input);//The file is empty.
		return 1;
	}

	//Saving the path of the list-file
	ListPath = local_Input;

	//Line-by-line reading of the list-file
	if(ReadOpenList(OpenList))
	{
		OpenList.close();
		return 1;
	}

	//Create an output file, the future fst-archive
	OpenReceiver.open(local_Output, ios::out|ios::binary);
	if(!OpenReceiver.is_open())
	{
		OpenList.close();
		OpenReceiver.close();
		remove(local_Output);
		SendAlert(3, local_Output);//Can't open output file.
		return 1;
	}

	//Saving path of the archive being packed
	ArchivePath = local_Output;

	//Putting packed data to new archive
	if(FillData())
	{
		OpenList.close();
		OpenReceiver.close();
		remove(local_Output);
		return 1;
	}

	//Putting head data to new archive
	FillHead();

	OpenList.close();
	OpenReceiver.close();

	return 0;
}
/// \brief Reading the list-file containing paths to files to be packed
/// \param[in] List File stream for reading the list of files to be packed
/// \return Error code: 0 - no errors, 1 - operation error
int FstPack::ReadOpenList(ifstream& List)
{
	//Create path for active directory
	cnv::unsint Position = 0;
	for(cnv::unsint i = 0; i < ListPath.size(); i++)
	{
		if(ListPath.at(i) == '\\')
		{
			Position = i + 1;
		}
	}
	std::string ActiveDirectory = ListPath.substr(0, Position);

	while(OpenList)
	{
		char PackCommand;
		std::string TempPath;

		//Read line from the list
		getline(List, TempPath);

		//Ignore empty line
		if(TempPath.size() == 0)
		{
			continue;
		}

		//Instruction processing
		if(TempPath.at(1) == 0x20 && TempPath.size() > 2)
		{
			TOCList.push_back(Entry());												//Add new cell into TOCList vector
			PackCommand = static_cast<char>(tolower(TempPath.at(0)));				//Get command symbol
			TempPath.erase(0, 2);													//Erase 2 first symbols (command symbol + single space)
			TOCList.at(AmountEntries).RealPath = ActiveDirectory + TempPath;		//Get full path to the file on the hard drive
			ifstream TryToOpen(TOCList.at(AmountEntries).RealPath);					//Create stream to open the file from the list
			if(TryToOpen)
			{
				TryToOpen.close();
				TOCList.at(AmountEntries).UncompressSize = chf::GetFileSize(TOCList.at(AmountEntries).RealPath);
				if(TOCList.at(AmountEntries).UncompressSize == 0)
				{
					SendAlert(4, TOCList.at(AmountEntries).RealPath);//The file is empty.
					return 1;
				}
				switch(PackCommand)
				{
					case 'c':		//Add as compressed file
						TOCList.at(AmountEntries).CompressFlag = CF_COMPRESS;
						break;

					case 'u':		//Add as uncompressed file
						TOCList.at(AmountEntries).CompressFlag = CF_UNCOMPRESS;
						break;

					default:		//Unknown command
						SendAlert(5, ListPath);//Instruction read error. Unknown command. Check if the command is entered correctly.
						return 1;
				}
				TOCList.at(AmountEntries).Path = TempPath;
			}
			else
			{
				SendAlert(6, ListPath);//Instruction read error. Packing file not found.
				return 1;
            }
		}
		else
		{
			SendAlert(7, ListPath);//Instruction read error. Incorrect entry.
			return 1;
		}
		AmountEntries++;
	}
	List.close();
	if(AmountEntries == 0)
	{
		SendAlert(8, ListPath);//No entries were received.
		return 1;
	}
	return 0;
}
/// \brief Putting packed data to new archive
/// \return Error code: 0 - no errors, 1 - operation error
int FstPack::FillData()
{
	OpenReceiver.seekp((AmountEntries * 4 * 262) + 4, std::ios_base::beg);
	for(cnv::unsint i = 0; i < AmountEntries; i++)
	{
		switch(TOCList.at(i).CompressFlag)
		{
			case CF_COMPRESS:	//Add as compressed file
				if(AddCompressedFile(i))
					return 1;
				break;

			case CF_UNCOMPRESS:	//Add as uncompressed file
				if(AddUncompressedFile(i))
					return 1;
				break;
		}
	}
	return 0;
}
/// \brief Appending compressed file-entry to new archive
/// \param[in] Count Ordinal number of the file being packed
/// \return Error code: 0 - no errors, 1 - operation error
/// \bug Strange allocation bug. Currently fixed by fixing the allocation limit. But no guarantees.
int FstPack::AddCompressedFile(cnv::unsint Count)
{
	ifstream AddedFile;

	//Opening the file for packing into archive
	AddedFile.open(TOCList.at(Count).RealPath.data(), ios::in|ios::binary);
	if(!AddedFile.is_open())
	{
		SendAlert(9, TOCList.at(Count).RealPath);//Can't open file.
		return 1;
	}

	//Checking the size for exceeding the limit
	const cnv::dword SizeLimit = 1024*1024*50;//50 MB
	if(TOCList.at(Count).UncompressSize > SizeLimit)
	{
		SendAlert(10, TOCList.at(Count).RealPath.data());//File limit exceeded. Files no larger than 50 MB are allowed.
		return 1;
		//This is a very strange memory allocation bug.
		//Some specific numbers lead to critical error - Access violation.
		//Now memory is allocated fixedly - only 50 MB.
		//Alas, files over this limit cannot be packed yet.
		//Perhaps cause of the error is in the type of the variable.
	}

	std::vector<cnv::byte> CopyBuffer(SizeLimit);//std::vector<cnv::byte> CopyBuffer(TOCList.at(Count).UncompressSize);

	//Reading content from the file being packed into CopyBuffer
	StreamRead(AddedFile, reinterpret_cast<char*>(CopyBuffer.data()), static_cast<int>(TOCList.at(Count).UncompressSize));
	AddedFile.close();

	//Getting compressed data
	//Since almost all data is text, we don't need to worry about data growth instead of compression, but it's technically possible
	std::vector<cnv::byte> CompressedBuffer(SizeLimit);//std::vector<cnv::byte> CompressedBuffer(TOCList.at(Count).UncompressSize);
	TOCList.at(Count).CompressSize = static_cast<cnv::dword>(LZCompress(CompressedBuffer.data(), CopyBuffer.data(), TOCList.at(Count).UncompressSize));

	//Saving offset into receive file
	std::streamoff FtellRespond = OpenReceiver.tellp();
	if(FtellRespond != -1)
	{
		TOCList.at(Count).DataOffset = static_cast<cnv::dword>(FtellRespond);
	}
	else
	{
		SendAlert(11, ArchivePath);//Invalid position indicator value was received in the archive file.
		return 1;
	}

	//Writing resulting data for packing into receive file
	OpenReceiver.write(reinterpret_cast<const char*>(CompressedBuffer.data()), TOCList.at(Count).CompressSize);

	return 0;
}
/// \brief Appending uncompressed file-entry to new archive
/// \param[in] Count Ordinal number of the file being packed
/// \return Error code: 0 - no errors, 1 - operation error
int FstPack::AddUncompressedFile(cnv::unsint Count)
{
	ifstream AddedFile;

	//Opening the file for packing into archive
	AddedFile.open(TOCList.at(Count).RealPath.data(), ios::in|ios::binary);
	if(!AddedFile.is_open())
	{
		SendAlert(12, TOCList.at(Count).RealPath);//Can't open file.
		return 1;
	}

	//Save the size of packed file
	TOCList.at(Count).CompressSize = TOCList.at(Count).UncompressSize;

	std::vector<cnv::byte> CopyBuffer(TOCList.at(Count).UncompressSize);

	//Reading content from the file being packed into CopyBuffer
	StreamRead(AddedFile, reinterpret_cast<char*>(CopyBuffer.data()), static_cast<int>(TOCList.at(Count).UncompressSize));
	AddedFile.close();

	//Saving offset and compression flag into receive file
	std::streamoff FtellRespond = OpenReceiver.tellp();
	if(FtellRespond != -1)
	{
		TOCList.at(Count).DataOffset = static_cast<cnv::dword>(FtellRespond);
	}
	else
	{
		SendAlert(13, ArchivePath);//Invalid position indicator value was received in the archive file.
		return 1;
	}

	//Writing resulting data for packing into receive file
	OpenReceiver.write(reinterpret_cast<const char*>(CopyBuffer.data()), TOCList.at(Count).UncompressSize);

	return 0;
}
/// \brief Putting head data to new archive
void FstPack::FillHead()
{
	//Put the write cursor at beginning of the file
	OpenReceiver.seekp(0, std::ios_base::beg);

	//Write amount of entries to the file
	for(int j = 0; j < 4; j++)
	{
		OpenReceiver << static_cast<cnv::byte>(AmountEntries >> 8 * j);
	}

	//Write all the data for the file header
	for(cnv::dword i = 0; i < AmountEntries; i++)
	{
		OpenReceiver.seekp((i * 262) + 4, std::ios_base::beg);
		for(int j = 0; j < 4; j++)
		{
			OpenReceiver << static_cast<cnv::byte>(TOCList.at(i).DataOffset >> 8 * j);
		}
		for(int j = 0; j < 4; j++)
		{
			OpenReceiver << static_cast<cnv::byte>(TOCList.at(i).CompressSize >> 8 * j);
		}
		for(int j = 0; j < 4; j++)
		{
			OpenReceiver << static_cast<cnv::byte>(TOCList.at(i).UncompressSize >> 8 * j);
		}
		OpenReceiver << TOCList.at(i).Path;
	}
    return;
}


/// \brief Clearing instance fields (unpack instance)
void FstUnpack::Clear()
{
	AmountEntries = 0;
	FileSize = 0;
	TOCList.erase(TOCList.begin(), TOCList.end());

    ReadBuffer.erase(ReadBuffer.begin(), ReadBuffer.end());
	DecompBuffer.erase(DecompBuffer.begin(), DecompBuffer.end());
	UncompressedSize = 0;

	if(OpenArchive.is_open())
		OpenArchive.close();
	if(OpenReceiver.is_open())
		OpenArchive.close();
	ArchivePath.erase(ArchivePath.begin(), ArchivePath.end());
}
/// \brief Loading an archive to the program memory
/// \param[in] local_Input Path to the file to unpack
/// \return Error code: 0 - no errors, 1 - operation error
int FstUnpack::Load(char* local_Input)
{
	//Clearing the instance fields
	Clear();

	//Trying to open the archive
	OpenArchive.open(local_Input, ios::in|ios::binary);
	if(!OpenArchive.is_open())
	{
		OpenArchive.close();
		SendAlert(14, local_Input);//Can't open input archive file. File does not exist or is locked. (Or the file path contains Unicode characters.)
		return 1;
	}

	//Saving path of the archive
	ArchivePath = local_Input;

	//Get the file size
	FileSize = chf::GetFileSize(OpenArchive);
	if(FileSize == 0)
	{
		OpenArchive.close();
		SendAlert(15, local_Input);//The file is empty.
		return 1;
	}

	//Get number of entries in the archive
	if(DwordRead(OpenArchive, AmountEntries))
	{
		OpenArchive.close();
		return 1;
	}

	//Resize table of contents array
	TOCList.resize(AmountEntries);

	//Get table of content data
   	for(cnv::dword i = 0; i < AmountEntries; i++)
	{
		//Read DataOffset
		if(DwordRead(OpenArchive, TOCList.at(i).DataOffset))
		{
			OpenArchive.close();
			return 1;
		}

		//Read CompressSize
		if(DwordRead(OpenArchive, TOCList.at(i).CompressSize))
		{
			OpenArchive.close();
			return 1;
		}

		//Read UncompressSize
		if(DwordRead(OpenArchive, TOCList.at(i).UncompressSize))
		{
			OpenArchive.close();
			return 1;
		}

		//Read Path
		TOCList.at(i).Path.resize(250);
		StreamRead(OpenArchive, TOCList.at(i).Path.data(), 250);

		//String optimization
		for(cnv::unsint j = 0; j < 250; j++)
		{
			if(TOCList.at(i).Path.at(j) == 0)
			{
				TOCList.at(i).Path.resize(j);
                break;
			}
		}

		//Calculate CompressFlag
		if(TOCList.at(i).UncompressSize == 0)
		{
			TOCList.at(i).CompressFlag = CF_EMPTY;
        }
		else if(TOCList.at(i).CompressSize == TOCList.at(i).UncompressSize)
		{
			TOCList.at(i).CompressFlag = CF_UNCOMPRESS;
		}
		else
		{
			TOCList.at(i).CompressFlag = CF_COMPRESS;
		}
	}

	OpenArchive.close();
	return 0;
}
/// \brief Extracting a single file from the archive
/// \param[in] Count Ordinal number of the file being unpacked
/// \return Error code: 0 - no errors, 1 - operation error
int FstUnpack::ExtractFromArchive(cnv::unsint Count)
{
	//Checking for loading the archive into memory
	if(ArchivePath.empty())
	{
		SendAlert(16, nullptr);//Archive not loaded into memory.
		return 1;
	}

	//Checking for compliance with boundaries of the archive table of contents
	if(AmountEntries <= Count)
	{
		SendAlert(17, nullptr);//Violating boundaries of the table of contents (Attempted to read outside the list limits).
		return 1;
	}

	//Trying to open the archive
	OpenArchive.open(ArchivePath, ios::in|ios::binary);
	if(!OpenArchive.is_open())
	{
		SendAlert(18, ArchivePath);//Can't open input archive file. File does not exist or is locked. (Or the file path contains Unicode characters.)
		return 1;
	}

	//Formation of variables for generating folder and file names when unpacking
	std::string SymbolOfDisk; SymbolOfDisk.resize(MAX_PATH);
	std::string PathOfDirectory; PathOfDirectory.resize(MAX_PATH);
	std::string NameOfFile; NameOfFile.resize(MAX_PATH);
	std::string ExtensionOfFile; ExtensionOfFile.resize(MAX_PATH);
	std::string NameOfFileUpdated; NameOfFileUpdated.resize(MAX_PATH);
	chf::SplitPath(ArchivePath, SymbolOfDisk, PathOfDirectory, NameOfFile, ExtensionOfFile);

	std::string DirectoryForExtract; DirectoryForExtract.resize(MAX_PATH);

	for(cnv::unsint i = 0; ; i++)
	{
		//Correct the unpacking path by checking the data from the archive (if necessary)
		cnv::unsint Position = 0;
		cnv::unsint k = 0;
		if(i != 0)
		{
			for(cnv::unsint j = 0; j < static_cast<cnv::unsint>(TOCList.at(Count).Path.size()); j++)
			{
				if(TOCList.at(Count).Path.at(j) == '\\')
				{
					Position = j + 1;
					k++;
					if(i == k)
						break;
				}
			}

			if(i - 1 == k)
				break;
		}

		if(k != 0)
			NameOfFileUpdated = NameOfFile + ExtensionOfFile + "_unpack\\" + TOCList.at(Count).Path.substr(0, Position);
		else
			NameOfFileUpdated = NameOfFile + ExtensionOfFile + "_unpack\\";

		//Getting file path assembly from parameters (DeepDirectory). This is the option when output folder is created
		std::string DeepDirectory; DeepDirectory.resize(MAX_PATH);
		_makepath(DeepDirectory.data(), SymbolOfDisk.data(), PathOfDirectory.data(), NameOfFileUpdated.data(), nullptr);
		chf::CreateDirectory(DeepDirectory);
		if(!(chf::IsDirectoryExists(DeepDirectory)))
		{
			OpenArchive.close();
			SendAlert(19, DeepDirectory);//Directory does not exist.
			return 1;
		}
	}

	//Formation of path to output file
	NameOfFileUpdated = NameOfFile + ExtensionOfFile + "_unpack\\";
	_makepath(DirectoryForExtract.data(), SymbolOfDisk.data(), PathOfDirectory.data(), NameOfFileUpdated.data(), nullptr);
	std::string Receiver; Receiver.resize(MAX_PATH);
	Receiver = DirectoryForExtract.data() + TOCList.at(Count).Path;

	//Trying to open output file
	OpenReceiver.open(Receiver, ios::out|ios::binary);
	if(!OpenReceiver.is_open())
	{
		OpenArchive.close();
		SendAlert(20, Receiver);//The file can't be opened or does not exist.
		return 1;
    }

	//Loading the required data segment from the archive
	ReadBuffer.resize(TOCList.at(Count).CompressSize);
	OpenArchive.seekg(TOCList.at(Count).DataOffset, std::ios_base::beg);
	StreamRead(OpenArchive, reinterpret_cast<char*>(ReadBuffer.data()), static_cast<int>(TOCList.at(Count).CompressSize));

	//Trying to unpack the file-entry if it is compressed
	if(TOCList.at(Count).CompressFlag == 1)
	{
		DecompBuffer.resize(TOCList.at(Count).UncompressSize + 16);//Adding some space just in case

		UncompressedSize = static_cast<cnv::dword>(LZDecompress(DecompBuffer.data(), ReadBuffer.data(), TOCList.at(Count).CompressSize));

		if(UncompressedSize == TOCList.at(Count).UncompressSize)
		{
			OpenReceiver.write(reinterpret_cast<const char*>(DecompBuffer.data()), TOCList.at(Count).UncompressSize);
		}
		else
		{
			SendAlert(21, Receiver);//The decompressed sizes of file and file-entry do not match.
			return 1;
		}
	}
	else if(TOCList.at(Count).CompressFlag == 0)
	{
		OpenReceiver.write(reinterpret_cast<const char*>(ReadBuffer.data()), TOCList.at(Count).CompressSize);
	}

	OpenReceiver.close();
	OpenArchive.close();

	if(chf::GetFileSize(Receiver) == 0 && TOCList.at(Count).CompressFlag != 2)
	{
		SendAlert(22, Receiver);//The created file does not exist or has size of zero.
		return 1;
	}
	return 0;
}
/// \brief Unpacking entire contents of the archive
/// \return Error code: 0 - no errors, 1 - operation error
int FstUnpack::UnpackEntireArchive()
{
	for(cnv::dword i = 0; i < AmountEntries; i++)
	{
		if(ExtractFromArchive(i))
			return 1;
	}
	return 0;
}
/// \brief Creating a list-file with contents of the archive
/// \return Error code: 0 - no errors, 1 - operation error
int FstUnpack::CreateListFile()
{
	//Checking for loading the archive into memory
	if(ArchivePath.empty())
	{
		SendAlert(23, nullptr);//Archive not loaded into memory.
		return 1;
	}

	//Formation of variables for generating folder and file names when unpacking
	std::string SymbolOfDisk; SymbolOfDisk.resize(MAX_PATH);
	std::string PathOfDirectory; PathOfDirectory.resize(MAX_PATH);
	std::string NameOfFile; NameOfFile.resize(MAX_PATH);
	std::string ExtensionOfFile; ExtensionOfFile.resize(MAX_PATH);
	std::string DirectoryForList; DirectoryForList.resize(MAX_PATH);
	chf::SplitPath(ArchivePath, SymbolOfDisk, PathOfDirectory, NameOfFile, ExtensionOfFile);

	//Create list-file of the same name in this folder
	std::string ListFileName; ListFileName.reserve(MAX_PATH);
	ListFileName = SymbolOfDisk + PathOfDirectory + NameOfFile + ExtensionOfFile + ".list";
	ofstream ListFromArchiveFile(ListFileName, ios::out);

	//Forming the list-file
	if(ListFromArchiveFile.is_open())
	{
		bool EndlCheck = false;
		for(cnv::unsint i = 0; i < AmountEntries; i++)
		{
			if(EndlCheck)
			{
				ListFromArchiveFile << std::endl;
			}
			else
			{
				EndlCheck = true;
			}
			if(TOCList.at(i).CompressFlag == CF_UNCOMPRESS)
			{
				ListFromArchiveFile << "u " << NameOfFile << ExtensionOfFile << "_unpack\\" << TOCList.at(i).Path;
			}
			else if(TOCList.at(i).CompressFlag == CF_COMPRESS)
			{
				ListFromArchiveFile << "c " << NameOfFile << ExtensionOfFile << "_unpack\\" << TOCList.at(i).Path;
			}
		}
		ListFromArchiveFile.close();
	}
	else
	{
		remove(ListFileName.data());
		SendAlert(24, ListFileName);//Attempt to create list failed. File does not exist or is locked. (Or the file path contains Unicode characters.)
		return 1;
	}
	return 0;
}


/// \brief Read block of data from file as DWORD
/// \param[in] local_Input File stream from which DWORD data is read
/// \param[out] local_Output DWORD vector for storing processed data
/// \param[in] Amount Number of DWORDs to read from the file stream
/// \return Error code: 0 - no errors, 1 - operation error
int DwordRead(ifstream& local_Input, std::vector<cnv::dword>& local_Output, cnv::unsint Amount)
{
	if(local_Output.size() < Amount)
	{
		SendAlert(25, nullptr);//The requested read size exceeds the buffer allocated for storage.
		return 1;
	}
	else
	{
		std::vector<char> Buffer(4 * Amount);
		local_Input.read(Buffer.data(), 4 * Amount);
		if(!local_Input.good())
		{
			if(local_Input.eof())
			{
				SendAlert(26, nullptr);//The file data ended before data block was read. The file is corrupted.
				return 1;
			}
			else if(local_Input.fail())
			{
				SendAlert(27, nullptr);//ReadStream - Logical error on input operation.
				return 1;
			}
			else if(local_Input.bad())
			{
				SendAlert(28, nullptr);//ReadStream - Read error on input operation.
				return 1;
			}
		}
		for(cnv::dword j = 0; j < Amount; j++)
		{
			cnv::dword BufferDword = 0;
			for(cnv::dword i = 0; i < 4; i++)
			{
				BufferDword += static_cast<cnv::dword>(static_cast<cnv::byte>(Buffer.at(i + (j * 4)))) * pow(0x100, i);
			}
			local_Output.at(j) = BufferDword;
		}
	}
	return 0;
}
/// \brief Read block of data from file as DWORD
/// \param[in] local_Input File stream from which DWORD data is read
/// \param[out] local_Output DWORD variable for storing processed data
/// \return Error code: 0 - no errors, 1 - operation error
int DwordRead(ifstream& local_Input, cnv::dword& local_Output)
{
	std::vector<char> Buffer(4);
	local_Input.read(Buffer.data(), 4);
	if(!local_Input.good())
	{
		if(local_Input.eof())
		{
			SendAlert(29, nullptr);//The file data ended before data block was read. The file is corrupted.
			return 1;
		}
		else if(local_Input.fail())
		{
			SendAlert(30, nullptr);//ReadStream - Logical error on input operation.
			return 1;
		}
		else if(local_Input.bad())
		{
			SendAlert(31, nullptr);//ReadStream - Read error on input operation.
			return 1;
		}
	}
	cnv::dword BufferDword = 0;
	for(cnv::unsint i = 0; i < 4; i++)
	{
		BufferDword += static_cast<cnv::dword>(static_cast<cnv::byte>(Buffer.at(i))) * pow(0x100, i);
	}
	local_Output = BufferDword;
	return 0;
}
/// \brief Read block of data from file
/// \param[in] InputStream File stream
/// \param[out] Buffer Buffer for outputting read data
/// \param[in] ReadSize Read data size
/// \return Error code: 0 - no errors, 1 - operation error
int StreamRead(ifstream& InputStream, char* Buffer, int ReadSize)
{
	InputStream.read(Buffer, ReadSize);
	if(!InputStream.good())
	{
		if(InputStream.eof())
		{
			SendAlert(32, nullptr);//The file data ended before data block was read. The file is corrupted.
			return 1;
		}
		else if(InputStream.fail())
		{
			SendAlert(33, nullptr);//StreamRead - Logical error on input operation.
			return 1;
		}
		else if(InputStream.bad())
		{
			SendAlert(34, nullptr);//StreamRead - Read error on input operation.
			return 1;
		}
	}
	return 0;
}
