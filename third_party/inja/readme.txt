nja 是一个面向现代 C++ 的模板引擎，其灵感源自Python 的Jinja。它拥有简洁而强大的模板语法，包含了你所需的所有变量、循环、条件语句、包含语句、回调函数和注释，并且可以根据需要进行嵌套和组合。当然，所有功能都已在持续集成 (CI) 环境中针对所有主流编译器进行了测试。以下是它的界面示例：

json data;
data["name"] = "world";

inja::render("Hello {{ name }}!", data); // Returns "Hello world!"