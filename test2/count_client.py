with open("your_log_file.txt", "r") as f:
    lines = f.readlines()

for line in lines:
    if "client" in line.lower():
        print(line.strip())
