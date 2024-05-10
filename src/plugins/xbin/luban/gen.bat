set GEN_CLIENT=dotnet .\Luban.ClientServer\Luban.ClientServer.dll

%GEN_CLIENT% -j cfg --^
 -d Defines\__root__.xml ^
 --input_data_dir Datas ^
 --output_code_dir Gen ^
 --gen_types code_cpp_bin ^
 -s all
pause

%GEN_CLIENT% -j cfg --^
 -d Defines\__root__.xml ^
 --input_data_dir Datas ^
 --output_code_dir Gen ^
 --gen_types code_typescript_bin ^
 -s all ^
 --typescript:bright_require_path ../bright
pause