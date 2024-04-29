set GEN_CLIENT=dotnet %LUBAN_CLIENTSERVER%

%GEN_CLIENT% -j cfg --^
 -d Defines\__root__.xml ^
 --input_data_dir Datas ^
 --output_code_dir Gen ^
 --gen_types code_cpp_bin ^
 -s all
pause