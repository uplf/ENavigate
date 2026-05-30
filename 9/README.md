# 🦞 龙虾管理系统 (Lobster Auth)

一个简洁的用户认证与管理 Web 应用，基于 Express + Handlebars + SQLite。

## 技术栈

| 层 | 选型 |
|---|---|
| 运行时 | Node.js ≥ 18 |
| 框架 | Express 5 |
| 模板引擎 | Handlebars (hbs) |
| 数据库 | SQLite (better-sqlite3) |
| 密码加密 | bcryptjs |
| Session | express-session |

## 快速启动

```bash
cd webauth/
npm install
node index.js
```

服务默认监听 `0.0.0.0:3002`，管理员默认账号 `admin / 123456`。

## 项目结构

```
webauth/
├── index.js                  # 入口：Express 配置、路由挂载、启动
├── db/
│   ├── init.js               # 数据库初始化、建表、种子数据
│   ├── model.js              # UserModel：用户 CRUD + 权限管理 + 站内信
│   └── project.js            # ProjectModel：项目 CRUD + 成员管理 + 邀请管理
├── background/               # 页面背景图片（与 .hbs 同名自动匹配）
├── public/                   # 静态资源（含 background/ 的符号链接）
static/                    # 手动上传的文件存储目录（upload 子目录）
├── projects-fs.js            # ProjectsFS：项目文件系统操作（目录初始化、软链接、文件/命令管理）
├── middlewares/
│   └── auth.js               # requireLogin / requireAdmin 中间件
│   └── basepath.js           # X-Forwarded-Prefix 反向代理支持
├── routes/
│   ├── auth.js               # 登录、注册、退出、个人中心、用户修改密码
│   ├── admin.js              # 管理员：用户管理 + 权限管理 API + 注册设置 + 资源管理（Xray/OpenClaw）
│   ├── message.js            # 站内信：收件箱、发件箱、写消息、未读计数
│   ├── translate.js          # 翻译：百度翻译 API 调用（中英日互译）
│   ├── storage.js           # 文件存储路由（文件夹浏览/新建/删除、上传/下载/预览/删除/重命名、在线文本编辑）
│   └── projects.js           # 项目管理：项目CRUD、成员管理、邀请、文件管理（含子文件夹/在线编辑）、命令执行
├── views/                    # Handlebars 视图模板
│   ├── layout.hbs            # 全局布局：navbar + 账户管理下拉 + 资源管理下拉 + 站内信入口
│   ├── login.hbs             # 登录页
│   ├── register.hbs          # 注册页
│   ├── home.hbs              # 首页（登录后欢迎页）
│   ├── admin/
│   │   ├── settings.hbs      # 注册设置：注册策略（单选卡片）+ 默认权限（toggle）
│   │   ├── users.hbs         # 用户管理列表（含管理权限弹窗）
│   │   ├── xray.hbs          # 路由服务：Xray 状态/启停/日志下载
│   │   ├── openclaw.hbs      # 助手服务：OpenClaw 状态 + 远程 Dashboard 登录指引
│   │   └── password.hbs      # 管理员修改密码
│   ├── message/
│   │   ├── inbox.hbs         # 收件箱（未读标记、发信人、主题、时间）
│   │   ├── sent.hbs          # 发件箱（收信人、已读/未读状态、时间）
│   │   ├── compose.hbs       # 写消息（收信人选择、主题、内容、权限校验）
│   │   └── detail.hbs        # 消息详情（自动标已读）
│   └── user/
│       ├── profile.hbs       # 个人中心（含权限状态）
│       └── password.hbs      # 用户修改密码
│   └── projects/
│       ├── index.hbs         # 项目列表（拥有的项目 + 参与的项目 + 待处理的邀请）
│       ├── create.hbs        # 新建项目
│       ├── detail.hbs        # 项目详情/控制台（成员管理、文件列表、命令列表、删除/退出）
│       ├── files.hbs         # 项目文件管理（上传/下载/删除/预览/重命名/在线编辑、子文件夹浏览）
│       ├── commands.hbs      # 项目命令管理（保存/运行/停止/查看日志）
│       └── invite.hbs        # 邀请成员
├── MIGRATE_LEGACY.md         # 旧功能（文件管理和运行程序）删除/迁移指南
└── data/
    └── webauth.db            # SQLite 数据库文件（自动生成）
```

## 数据模型

### users 表

| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK | 自增主键 |
| uid | TEXT UNIQUE | 登录账号 |
| password_hash | TEXT | bcrypt 哈希 |
| role | TEXT | `admin` 或 `user` |
| can_login | INTEGER | 0/1，管理员审核开关 |
| is_active | INTEGER | 0/1，账号启用/禁用 |
| delete_requested | INTEGER | 0/1，用户是否已申请删除账号 |
| created_at | TEXT | 注册时间（本地时区） |
| updated_at | TEXT | 最后更新时间 |

### app_settings 表

| 字段 | 类型 | 说明 |
|---|---|---|
| key | TEXT PK | 配置键名 |
| value | TEXT | 配置值 |

**内置配置项：**

| 键 | 值示例 | 说明 |
|---|---|---|
| `active_model` | `deepseek/deepseek-chat` | 当前使用的 AI 模型 |
| `register_policy` | `disabled` / `pending` / `open` | 注册策略 |
| `default_permissions` | `{"view_static":1,...}` | JSON，新用户注册时自动授予的默认权限 |
| `storage_global_enabled` | `1` / `0` | 文件存储服务全局开关 |

### user_permissions 表

| 字段 | 类型 | 说明 |
|---|---|---|
| user_id | INTEGER PK | FK → users(id)，级联删除 |

### projects 表

| 字段 | 类型 | 说明 |
|---|---|---|
| id | TEXT PK | UUID v4，由服务端生成 |
| name | TEXT UNIQUE | 项目名称（字母数字横线下划线） |
| owner_uid | TEXT FK | 创建者（FK → users.uid） |
| created_at | TEXT | 创建时间（本地时区） |

### project_members 表

| 字段 | 类型 | 说明 |
|---|---|---|
| project_id | TEXT PK | FK → projects(id)，级联删除 |
| uid | TEXT PK | FK → users(uid) |
| role | TEXT | `owner` / `editor` / `reader` |
| joined_at | TEXT | 加入时间（本地时区） |

### project_invitations 表

| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK | 自增主键 |
| project_id | TEXT FK | FK → projects(id)，级联删除 |
| from_uid | TEXT FK | 邀请人 |
| to_uid | TEXT FK | 被邀请人 |
| role | TEXT | 邀请时指定的角色（`editor` / `reader`） |
| status | TEXT | `pending` / `accepted` / `rejected` |
| created_at | TEXT | 邀请时间（本地时区） |

### messages 表

| 字段 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK | 自增主键 |
| from_uid | TEXT FK | 发信人（FK → users.uid） |
| to_uid | TEXT FK | 收信人（FK → users.uid） |
| subject | TEXT | 消息主题（可选） |
| body | TEXT | 消息正文 |
| is_read | INTEGER | 0=未读，1=已读 |
| created_at | TEXT | 发送时间（本地时区） |

### user_permissions 表

| 字段 | 类型 | 说明 |
|---|---|---|
| user_id | INTEGER PK | FK → users(id)，级联删除 |
| view_static | INTEGER | 0/1，静态文件查看权限 |
| storage | INTEGER | 0/1，文件存储权限 |
| msg_admin | INTEGER | 0/1，站内信 — 管理员通信 |
| msg_user | INTEGER | 0/1，站内信 — 用户通信 |
| debug_proto | INTEGER | 0/1，协议调试功能 |
| translate | INTEGER | 0/1，翻译服务 |
| project_use | INTEGER | 0/1，项目使用权限（访问项目管理功能） |
| project_create | INTEGER | 0/1，新建项目权限 |
| created_at | TEXT | 权限记录创建时间 |
| updated_at | TEXT | 最后更新时间 |

> 新增权限只需在表中加字段（`perm_xxx INTEGER NOT NULL DEFAULT 0`），并在 `UserModel.setPermission()` 的 `allowed` 数组中注册字段名即可。无需改表结构之外的代码。
>
> 同时需要在「注册设置」页面的默认权限配置区域补全新字段的展示（`views/admin/settings.hbs`），并在 `UserModel.setDefaultPermission()` 中注册字段名。
>
> 如果新增权限需要在 `index.js` 的 `permLabel` Handlebars helper 中注册中文标签，否则默认权限配置页面会显示字段名本身。

### 用户删除账号

- `UserModel.requestDelete(uid)` — 用户提交删除申请（设置 `delete_requested = 1`）
- `UserModel.cancelDelete(uid)` — 用户或管理员取消删除申请（设置 `delete_requested = 0`）
- `UserModel.isDeleteRequested(uid)` — 查询指定用户是否已申请删除
- `UserModel.deleteUser(uid)` — 永久删除用户及关联数据（外键 CASCADE 自动清理权限和站内信）

## 路由一览

### 静态文件（公网展示）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | /static/lobster | 龙虾信息展示页（静态 HTML） |

### 公共（无需登录）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/login` | 登录页 |
| POST | `/login` | 登录提交 |
| GET | `/register` | 注册页 |
| POST | `/register` | 注册提交 |
| GET | `/logout` | 退出登录（需 session） |

### 所有人（需登录）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | 首页欢迎页 |
| GET | `/user/profile` | 个人中心 |
| GET/POST | `/user/password` | 普通用户修改密码 |
| POST | `/user/request-delete` | 普通用户申请删除账号 |
| POST | `/user/cancel-delete` | 普通用户取消删除申请 |
| GET | `/message` | 收件箱 |
| GET | `/message/sent` | 发件箱 |
| GET | `/message/compose` | 写消息页 |
| POST | `/message/compose` | 发送消息 |
| GET | `/message/read/:id` | 消息详情（自动标已读） |
| GET | `/message/unread` | 未读消息数量（JSON，供 navbar 红点使用） |
| GET | `/translate` | 翻译服务页（需 translate 权限） |
| POST | `/translate/api` | 翻译接口（JSON，body: `{q, from, to}`） |
| GET | `/projects` | 项目列表（我创建的 + 我参与的 + 待处理邀请） |
| GET | `/projects/create` | 新建项目表单页 |
| POST | `/projects/create` | 提交创建项目 |
| GET | `/projects/:id` | 项目详情/控制台 |
| GET | `/projects/:id/files` | 项目文件管理页（支持 `?path=` 子文件夹浏览） |
| POST | `/projects/:id/files/upload` | 上传文件到项目（multipart/form-data，支持 `_path` 指定子文件夹） |
| POST | `/projects/:id/files/mkdir` | 新建文件夹（JSON，body: `{name, _path}`，仅 Editor/Owner） |
| DELETE | `/projects/:id/files/folder/:folderName` | 删除文件夹（递归，支持 `?path=`，仅 Editor/Owner） |
| GET | `/projects/:id/files/download/:filename` | 下载项目文件（支持 `?path=` 子文件夹） |
| GET | `/projects/:id/files/preview/:filename` | 预览项目文件（支持 `?path=` 子文件夹） |
| POST | `/projects/:id/files/rename` | 重命名文件或文件夹（JSON，body: `{oldName, newName, _path}`，仅 Editor/Owner） |
| DELETE | `/projects/:id/files/:filename` | 删除项目文件（支持 `?path=` 子文件夹，仅 Editor/Owner） |
| GET | `/projects/:id/files/edit/:filename` | 读取文件内容用于在线编辑（JSON，支持 `?path=`，仅 Editor/Owner） |
| POST | `/projects/:id/files/save` | 保存编辑后的文件内容（JSON，body: `{filename, content, _path}`，仅 Editor/Owner） |
| GET | `/projects/:id/commands` | 项目命令执行页（卡片式 UI + WebSocket 实时日志） |
| GET | `/projects/:id/commands/list` | 命令列表（JSON，只读） |
| POST | `/projects/:id/tasks/:taskId/stop` | 停止正在运行的任务 |
| GET | `/projects/:id/tasks` | 获取任务列表 |
| GET | `/projects/:id/tasks/:taskId/logs` | 获取任务日志（支持 `?since=` 增量拉取） |
| GET | `/projects/:id/invite` | 邀请成员页 |
| POST | `/projects/:id/invite` | 发送邀请（JSON，body: `{uid, role}`） |
| GET | `/projects/invitations/count` | 待处理邀请数量（JSON，供 navbar 红点使用） |
| POST | `/projects/invitations/:id/accept` | 接受邀请 |
| POST | `/projects/invitations/:id/reject` | 拒绝邀请 |
| POST | `/projects/:id/members/remove` | 移除成员（JSON，body: `{uid}`，仅 Owner） |
| POST | `/projects/:id/members/role` | 修改成员角色（JSON，body: `{uid, role}`，仅 Owner） |
| POST | `/projects/:id/leave` | 退出项目 |
| POST | `/projects/:id/delete` | 删除项目（仅 Owner） |
| GET | `/storage` | 文件存储主页（支持 `?path=` 子文件夹浏览） |
| POST | `/storage/upload` | 上传文件（multipart/form-data，支持 `_path` 指定子文件夹） |
| POST | `/storage/mkdir` | 新建文件夹（JSON，body: `{name, _path}`，需编辑权限） |
| DELETE | `/storage/folder/:folderName` | 删除文件夹（递归，支持 `?path=` 参数，需编辑权限） |
| GET | `/storage/download/:filename` | 下载文件（支持 `?path=` 子文件夹） |
| GET | `/storage/preview/:filename` | 预览文件（图片/文本/PDF，支持 `?path=` 子文件夹） |
| POST | `/storage/rename` | 重命名文件或文件夹（JSON，body: `{oldName, newName, _path}`，需编辑权限） |
| DELETE | `/storage/:filename` | 删除文件（支持 `?path=` 子文件夹，需编辑权限） |
| GET | `/storage/edit/:filename` | 读取文件内容用于在线编辑（JSON，支持 `?path=`，需编辑权限） |
| POST | `/storage/save` | 保存编辑后的文件内容（JSON，body: `{filename, content, _path}`，需编辑权限） |

### 管理员（需登录 + admin 角色）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/admin/users` | 用户管理列表 |
| POST | `/admin/users/toggle-login` | 切换允许登录 |
| POST | `/admin/users/toggle-active` | 启用/禁用账号 |
| POST | `/admin/users/reset-password` | 重置用户密码 |
| GET/POST | `/admin/password` | 管理员修改密码 |
| GET | `/admin/users/:uid/permissions` | 获取用户权限（JSON） |
| POST | `/admin/users/:uid/permissions` | 设置用户权限（JSON，body: `{field, value}`） |
| GET | `/admin/settings` | 注册设置页（注册策略 + 默认权限） |
| POST | `/admin/settings/register-policy` | 设置注册策略（JSON，body: `{value}`） |
| POST | `/admin/settings/default-permission` | 设置默认权限（JSON，body: `{field, value}`） |
| POST | `/admin/settings/storage-global` | 设置文件存储全局开关（JSON，body: `{enabled}`） |
| POST | `/admin/users/delete` | 管理员删除已申请注销的用户 |
| POST | `/admin/users/cancel-delete-request` | 管理员取消用户的删除申请 |
| GET | `/admin/xray` | 路由服务管理页 |
| GET | `/admin/xray/status` | 获取 Xray 运行状态（JSON） |
| POST | `/admin/xray/start` | 开启 Xray 服务 |
| POST | `/admin/xray/stop` | 关闭 Xray 服务 |
| POST | `/admin/xray/restart` | 重启 Xray 服务 |
| GET | `/admin/xray/log/:name` | 下载日志文件（access / error） |
| GET | `/admin/openclaw` | OpenClaw 管理页 |
| GET | `/admin/openclaw/status` | 获取 OpenClaw 运行状态（JSON） |

## 中间件

### requireLogin

检查 `req.session.user`，未登录重定向到 `/login`，已登录将用户信息注入 `res.locals.user`。

### requireAdmin

在 `requireLogin` 之后使用，检查 role 是否为 `admin`，否则返回 403。

### basePath

从 `X-Forwarded-Prefix` 请求头读取反向代理基础路径（如 `/app`），自动：
- 在 `res.redirect()` 中为以 `/` 开头的路径拼接前缀
- 在模板中注入 `{{basePath}}` 变量供视图使用

> 所有视图内的链接和表单 action 均使用 `{{basePath}}` 前缀拼接，反向代理到子路径下也能正常工作。

## 关键逻辑

### 用户注册流程

1. 任何人可注册（`admin` 账号除外），但受「注册策略」控制
2. 注册策略有三种模式（管理员在「账户管理 → 注册设置」中切换）：
   - **禁止注册（disabled）**：注册提交时拒绝，提示「系统当前禁止新用户注册」
   - **待审核（pending）**：用户可成功注册，但需管理员手动「允许登录」后方可使用（默认模式）
   - **直接注册（open）**：注册成功后自动设置 `can_login = 1`，用户可直接登录

### 登录验证链路

```
检查 uid 是否存在 → 检查 is_active → 检查 can_login（普通用户）→ 检查密码 → 写入 session → 重定向到首页
```

### 权限控制

- **Navbar 一级导航**：管理员看到「首页」「服务资源」「账户管理」「资源管理」；普通用户看到「首页」「服务资源」
- **服务资源下拉**（所有人）：「文件存储」
- **账户管理下拉**（管理员）：「用户管理」「注册设置」
- **资源管理下拉**（管理员）：「路由服务」「助手服务」
- **用户下拉菜单**：管理员看到「站内信」「修改密码」「退出登录」；普通用户看到「站内信」「个人中心」「修改密码」「退出登录」
- **路由**：`/admin/*` 受 `requireAdmin` 保护，普通用户访问返回 403
- **站内信入口**：所有登录用户均可在用户名下拉菜单中看到「站内信」，点击进入收件箱

### 项目管理 — 架构设计

采用**数据库薄层 + 文件系统厚层**的混合架构：

#### 数据库层（薄层）
- `projects` 表：仅存储 UUID、项目名称、创建者
- `project_members` 表：用户-项目-角色关联（owner/editor/reader）
- `project_invitations` 表：邀请记录（pending/accepted/rejected）

#### 文件系统层（厚层）
- **项目根目录**：`/home/ubuntu/projects/<uuid>/`
- **元数据**：`.project/config.json`（项目名、描述等）
- **成员快照**：`users.json`（冗余存储，加速读取）
- **命令配置**：`commands/commands.json`（单个 JSON 文件，通过服务器文件系统管理）
- **文件存储**：`files/<filename>`（支持子文件夹层级，`files/<subdir>/<filename>`）
- **文件操作**：上传（上传到子文件夹）、下载、预览、删除、重命名（含文件夹）
- **文件夹操作**：新建文件夹、删除文件夹（递归）、重命名文件夹
- **在线编辑**：读取文件内容（GET edit）、保存编辑内容（POST save），支持子文件夹路径
- **用户软链接**：`/home/<uid>/projects/<uuid>` → `/home/ubuntu/projects/<uuid>/`（创建项目时自动创建，失败不阻塞）
- **配置常量**：`projects-fs.js` 中的 `PROJECTS_ROOT`（可通过 `PROJECTS_ROOT` 环境变量覆盖）

#### 功能级权限两道门

在进入项目功能时，系统先检查**功能级权限**（`user_permissions` 表），通过后再根据**成员角色**进行细粒度控制：

| 权限 | 字段 | 检查时机 |
|---|---|---|
| 项目使用 | `project_use` | 访问 `/projects` 列表页及所有项目相关页面时检查 |
| 新建项目 | `project_create` | 访问 `/projects/create` 及提交创建时检查 |

- 无 `project_use` 权限的用户访问项目页面会看到错误提示
- 有 `project_use` 但无 `project_create` 的用户可以查看和参与已有项目，但不能创建新项目
- 管理员在「注册设置」中可配置新用户的默认项目权限，在「用户管理」中可逐个用户调整

#### 成员权限矩阵

| 操作 | Owner | Editor | Reader |
|---|---|---|---|
| 修改项目名/删除项目 | ✅ | ❌ | ❌ |
| 邀请/移除成员 | ✅ | ✅（邀请） | ❌ |
| 修改成员角色 | ✅ | ❌ | ❌ |
| 上传/删除/重命名文件 | ✅ | ✅ | ❌ |
| 新建/删除文件夹 | ✅ | ✅ | ❌ |
| 在线编辑文件 | ✅ | ✅ | ❌ |
| 下载/预览文件 | ✅ | ✅ | ✅ |
| 执行命令 | ✅ | ✅ | ❌ |
| 查看命令列表和文件列表 | ✅ | ✅ | ✅ |
| 退出项目 | ✅ | ✅ | ✅ |

#### 命令执行
- 命令配置只能通过服务器文件系统管理：`<PROJECTS_ROOT>/<uuid>/commands/commands.json`
- Web 端只读查看，无法创建/编辑/删除任何命令
- 页面完全照搬「运行程序」的风格（卡片式 UI + WebSocket 实时日志）
- 命令以 `child_process.spawn` 异步执行
- 每个任务分配唯一 `taskId`（格式：`proj_<uuid>_<key>_<timestamp>`）
- 支持参数注入：在命令中使用 `${param_name}`，执行时传入参数值
- 支持环境变量注入：通过 `ARG_<NAME>` 环境变量传递参数
- WebSocket 实时推送日志，支持任务列表过滤和终止操作

#### 邀请流程
1. Owner/Editor 发送邀请 → 写入 `project_invitations`（status='pending'）
2. 受邀用户登录后在项目列表页顶部看到待处理邀请提示
3. 受邀用户点击「接受」或「拒绝」
4. 接受后自动加入 `project_members`，邀请状态标记为 `accepted`

#### 导航栏集成
- 导航栏「项目」链接 → 项目列表页 `/projects`
- navbar 红点：通过 AJAX 请求 `/projects/invitations/count` 获取待处理邀请数，>0 时在导航栏显示红色角标

### 站内信逻辑

站内信对所有登录用户开放，发送前校验权限：

- **发送给管理员**：需拥有 `msg_admin` 权限
- **发送给普通用户**：需拥有 `msg_user` 权限
- 权限不足时表单提交被拦截并显示错误，不能发送消息
- 消息发送后立即标记为收件人未读状态
- 收件人查看消息详情时自动标记为已读（`is_read = 1`）
- Navbar 加载时通过 AJAX 请求 `/message/unread` 获取未读数，>0 时显示红色角标

### 权限集体系

系统通过独立的 `user_permissions` 表管理细粒度功能权限，每个权限为独立的 0/1 字段，便于扩展：

| 权限 | 字段名 | 说明 |
|---|---|---|
| 静态文件查看 | `view_static` | 允许查看系统中的静态文件 |
| 文件存储 | `storage` | 允许使用文件存储服务（上传/下载/删除） |
| 站内信 — 管理员通信 | `msg_admin` | 允许与管理员通信 |
| 站内信 — 用户通信 | `msg_user` | 允许与其他普通用户通信 |
| 协议调试功能 | `debug_proto` | 允许使用协议调试工具 |
| 翻译服务 | `translate` | 允许使用百度翻译功能（中/英/日互译） |
| 项目使用 | `project_use` | 允许访问项目管理功能，查看和参与项目 |
| 新建项目 | `project_create` | 允许创建新的项目 |

**管理入口：** 管理员在「用户管理」页面点击每行后的「管理权限」按钮，弹出模态窗以开关设置。

**用户视角：** 登录用户在「个人中心」页面底部查看自己的权限状态（已授权/未授权）。

**扩展指引：** 新增权限时：
1. 在 `db/init.js` 的 `CREATE TABLE user_permissions` 中加字段
2. 在 `model.js` 的 `setPermission()` 中的 `allowed` 数组注册字段名
3. 在 `views/admin/users.hbs` 的 JS 中 `fields` 数组添加条目
4. 在 `views/user/profile.hbs` 表格中添加对应行
5. 在 `index.js` 的 `permLabel` helper 中注册中文标签（label + desc）
6. 在 `views/admin/settings.hbs` 的 `permLabel` 调用会自动渲染，因为 helper 已注册
7. 在 `routes/projects.js` 中添加或修改 `requireProjectUse`/`requireProjectCreate` 等权限检查中间件
8. 在 `db/model.js` 中 `UserModel.setPermission()`/`setDefaultPermission()`/`setDefaultPermissionsFromMap()` 的 `allowed` 数组中注册字段名

## 背景图 & 主题色系统

### 概述

系统支持为每个页面自动匹配背景图片，并提取图片主色调应用于页面的 UI 组件（卡片、弹窗、按钮、上传区域等），实现视觉与内容主题的统一。

**无需任何额外配置**，只需在 `background/` 目录下放置与视图同名的图片文件，即可自动生效。

### 工作原理

1. **后端（`index.js`）**：在路由注册前注入一个中间件，劫持 `res.render()`，自动提取视图文件名作为 `viewKey` 注入模板变量（如 `login.hbs` → `viewKey='login'`）
2. **前端（`layout.hbs`）**：页面加载时 JS 根据 `viewKey` 依次尝试加载 `background/<viewKey>.jpg`、`.jpeg`、`.png`、`.webp`、`.gif`
3. 找到匹配图片后：
   - 将图片设为页面全屏背景（`background-size: cover; background-position: center center` — 单图居中铺满，不拼接）
   - 通过 `::before` 伪元素实现 16px 高斯模糊
   - 利用 Canvas 将图片缩放到 32×32 后采样，直方图量化提取**主色**和**平均色**
   - 根据主色动态生成 CSS，将所有 `.card`、`.modal-card`、`.upload-area` 等主要 UI 容器替换为半透明主色 + `backdrop-filter: blur()` 毛玻璃效果
   - 根据主色亮度自动适配文字颜色（亮色背景用深色字，暗色背景用浅色字）

### 使用方法

```
webauth/
├── views/
│   ├── login.hbs          ← viewKey = 'login'
│   ├── home.hbs           ← viewKey = 'home'
│   └── admin/
│       └── users.hbs      ← viewKey = 'users'
└── background/
    ├── login.jpg           ← login.hbs 自动使用此图
    ├── home.png            ← home.hbs 自动使用此图
    └── users.webp          ← users.hbs 自动使用此图
```

**规则：**
- 图片文件名（不含扩展名）必须与 `.hbs` 文件名完全一致
- 支持的格式：`.jpg`、`.jpeg`、`.png`、`.webp`、`.gif`（按此顺序尝试，优先匹配先找到的）
- 没有匹配图片的页面保持原有样式不受影响

### 效果

| 特性 | 说明 |
|---|---|
| 背景 | 单张图片 `cover` 铺满全屏，居中不重复，16px 高斯模糊 |
| 卡片 | 半透明主色调 + `backdrop-filter` 毛玻璃，自适应文字颜色 |
| 弹窗 | 与卡片相同的毛玻璃效果，视觉统一 |
| 上传区域 | 虚线边框 + 半透明背景，与主题色融合 |
| 文本 | 根据主色亮度自动选择深色/浅色，确保可读性 |

### 文件结构

`background/` 文件夹通过符号链接暴露到 `public/` 静态目录下，浏览器可直接通过 `/background/<filename>` 访问。

## 视图模板说明

### layout.hbs（全局布局）

使用 Handlebars 条件渲染实现角色感知的导航：
- `{{#if (eq user.role 'admin')}}` 控制管理员专属菜单项
- PC：横向 navbar，含一级链接和下拉菜单
- 手机（768px 以下）：Hamburger 按钮 + 左侧抽屉侧边栏
  - 导航分组：导航 → 服务资源 → 账户管理/资源管理（仅管理员）→ 底部用户信息
  - 点击侧边栏链接或遮罩层自动关闭
  - 站内信未读计数在抽屉内也同步显示
- 用户下拉菜单通过纯 CSS + 少量 JavaScript 实现（无需 jQuery）
- 点击外部区域自动关闭下拉

### home.hbs（首页）

根据 `user.role` 显示不同的快捷入口按钮，底部有功能概览表格。

## 注册设置（管理员）

### admin/settings.hbs

管理员通过「账户管理」下拉菜单点击「注册设置」进入，页面分为三个配置区域：

#### 📂 文件存储服务

全局开关，关闭后所有用户（包括管理员）均无法访问文件存储功能。通过 toggle switch 即时切换，影响全站。

存储于 `app_settings` 表的 `storage_global_enabled` 键。

#### 📝 注册策略

通过三个单选卡片选择，点击即生效（AJAX 保存），无需额外提交：

| 选项 | 值 | 行为 |
|---|---|---|
| 🚫 禁止注册 | `disabled` | 注册提交时拒绝，提示「系统当前禁止新用户注册」 |
| ⏳ 待审核 | `pending` | 用户可成功注册，`can_login = 0`，需管理员手动开启 |
| ✅ 直接注册 | `open` | 注册成功后自动 `can_login = 1`，用户直接登录 |

后台存储于 `app_settings` 表的 `register_policy` 键。

#### 🔑 默认权限配置

新用户注册时自动获得的权限，通过 toggle switch 即时开关，影响后续注册的用户。

存储于 `app_settings` 表的 `default_permissions` 键，格式为 JSON 对象：
```json
{"view_static":1,"storage":1,"msg_admin":1,"msg_user":1,"debug_proto":1}
```

**权限字段与 `user_permissions` 表的列一一对应**，新增字段需同时修改此处的默认值。

### 登录页适配

登录页会根据注册策略显示/隐藏「注册」入口链接：

| 策略 | 登录页底部显示 |
|---|---|
| `disabled` | 「系统目前关闭注册，请联系管理员开通账号」（无链接） |
| `pending` / `open` | 「还没有账号？注册」（带链接） |

管理员「账户管理」下拉菜单包含两个入口：
- **用户管理** → `/admin/users`
- **注册设置** → `/admin/settings`

管理员「资源管理」下拉菜单包含两个入口：
- **路由服务** → `/admin/xray`
- **助手服务** → `/admin/openclaw`

所有人「服务资源」下拉菜单包含一个入口：
- **文件存储** → `/storage`（受全局开关 + 用户权限双重控制）

两组下拉均使用独立的 `navbar-dropdown` 组件，互不干扰，点击外部自动关闭。

### admin/xray.hbs（路由服务管理）

展示 Xray (VLESS) 服务的完整管理界面：

| 功能 | 说明 |
|---|---|
| 状态面板 | 运行状态、开机自启、PID、内存占用、运行时长 |
| 服务启停 | 开启/关闭/重启三个按钮，实时反馈 |
| 日志下载 | 下载 access.log 和 error.log（通过 sudo cat 以 spawn 流式传输） |

所有操作通过 `sudo systemctl` 执行，需提前配置 sudoers 免密码权限。

### admin/openclaw.hbs（助手服务管理）

展示 OpenClaw 网关服务的运行信息：
- 运行状态、版本号、PID、CPU/内存占用、运行时长
- 配置了外部访问链接

页面底部包含 **远程 Dashboard 登录指引** 板块，分两步引导：

| 步骤 | 操作 | 说明 |
|---|---|---|
| Step 1 | `ssh -N -L 18789:127.0.0.1:18789 ubuntu@<公网IP>` | 建立 SSH 隧道，将服务器 18789 端口转发到本地 |
| Step 2 | `http://localhost:18789/#token=xxx` | 隧道建立后浏览器访问该地址，携带 token 自动登录 Dashboard |

- 每条指令都有 **📋 复制按钮**，点击即复制到剪贴板
- SSH 隧道命令中的 `<host>` 自动替换为服务器公网 IP
- Dashboard URL 通过后端执行 `openclaw dashboard` 动态获取，携带完整 token

### 用户删除账号流程

1. 普通用户在**个人中心**页面点击「申请删除账号」按钮（需确认弹窗）
2. 系统将该用户标记为 `delete_requested = 1`
3. 用户个人中心显示「已申请删除」状态，按钮变为「取消删除申请」
4. 管理员在**用户管理**页面：
   - 「删除申请」列显示红底「已申请」标签
   - 「操作」列出现红色「删除」按钮（仅对已申请用户可见）
   - 管理员可点击「取消申请」按钮撤销用户的删除申请
5. 管理员点击「删除」后，该用户及其关联数据（权限、站内信等）被永久删除（外键 CASCADE）
6. 在管理员未处理前，用户可随时在个人中心自行取消删除申请

### admin/users.hbs（用户管理列表）

- PC 端以表格展示，包含 ID、账号、角色、允许登录、状态、注册时间、删除申请、操作
- 手机端自动切换为卡片列表（768px 以下），每个用户一张卡片纵向排列
  - 角色和注册时间列在手机端不显示
  - 信息/状态/操作/删除申请分多行展示，不会挤在一行
- 「删除申请」列显示用户是否已申请注销，已申请的带有「取消申请」按钮供管理员操作
- 「操作」列：仅当用户 `delete_requested = 1` 时显示红色「删除」按钮
- 每行/每卡操作栏包含「管理权限」按钮
- 点击后弹出模态窗，通过 AJAX 加载用户当前权限
- 使用 toggle switch 开关，即时保存（POST JSON 到服务器）
- 弹窗点击遮罩可关闭

### user/profile.hbs（个人中心）

在基本信息下方新增「我的权限」卡片，以表格列出所有权限及其状态。

用户可在此页面提交「申请删除账号」（需确认弹窗），申请后状态变更为「已申请删除」等待管理员处理。
在管理员未确认前，用户可随时点击「取消删除申请」撤销。

## 反向代理部署

> 当前服务器通过 nginx 在 443 端口的 `/app` 路径反代到本服务的 `3002` 端口。

**nginx 配置参考：**
```nginx
# 主代理 — /app/* -> 后端
location /app/ {
  rewrite ^/app(/.*)$ $1 break;
  proxy_pass http://127.0.0.1:3002;
  proxy_http_version 1.1;
  proxy_set_header Upgrade $http_upgrade;
  proxy_set_header Connection "upgrade";
  proxy_read_timeout 86400s;
  proxy_set_header Host $host;
  proxy_set_header X-Real-IP $remote_addr;
  proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
  proxy_set_header X-Forwarded-Proto $scheme;
  proxy_set_header X-Forwarded-Prefix /app;
  client_max_body_size 100m;
}

# 内部路由 fallback — 支持直接访问 /projects/xxx 等路径
# 无需手动拼 /app 前缀
location ~ ^/(login|register|logout|projects|admin|message|storage|translate|runner|user|static) {
  rewrite ^ /app$uri last;
  proxy_pass http://127.0.0.1:3002;
  proxy_http_version 1.1;
  proxy_set_header Upgrade $http_upgrade;
  proxy_set_header Connection "upgrade";
  proxy_read_timeout 86400s;
  proxy_set_header Host $host;
  proxy_set_header X-Real-IP $remote_addr;
  proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
  proxy_set_header X-Forwarded-Proto $scheme;
  proxy_set_header X-Forwarded-Prefix /app;
  client_max_body_size 100m;
}
```

`X-Forwarded-Prefix` 头确保 `res.redirect()` 和视图模板中的链接自动拼接 `/app` 前缀。

> ⚠️ 所有视图内的链接、表单 action、以及 JavaScript 中的 `fetch()` 调用均使用 `{{basePath}}` 前缀拼接，确保反向代理到子路径下也能正常工作。

> 🔄 **内部路由 fallback**：nignx 配置了一个 regex location，自动将 `/login`、`/projects/xxx`、`/admin/xxx` 等直接访问内部 rewrite 到 `/app` 路径下处理。这意味着用户无论通过 `/app/projects/xxx` 还是 `/projects/xxx` 都能正常访问，不会有 404。

## 开发指南

### 添加新功能

1. 在 `routes/` 下添加路由文件，或扩充已有路由
2. 在 `views/` 下添加对应的 `.hbs` 模板
3. 在 `index.js` 中挂载路由
4. 如需新数据表，在 `db/init.js` 的 `initDb()` 中追加 `CREATE TABLE`

### 添加项目管理相关功能

项目管理采用**数据库薄层 + 文件系统厚层**设计：
- 数据库层：`db/project.js` 中的 `ProjectModel` 类处理 CRUD
- 文件系统层：`projects-fs.js` 中的 `ProjectsFS` 类处理目录/文件/命令
- 路由层：`routes/projects.js` 聚合两个模块
- 视图层：`views/projects/` 下 6 个模板文件

如需新增项目功能模块：
1. 数据库：在 `db/init.js` 的 `initDb()` 中加表，在 `db/project.js` 中加方法
2. 文件系统：在 `projects-fs.js` 中加读写操作
3. 路由：在 `routes/projects.js` 中加路由（注意 `/create` 必须在 `/:id` 之前注册，避免被通配符路由拦截）
4. 视图：在 `views/projects/` 下加模板，所有链接必须使用 `{{basePath}}` 前缀

### 文件存储服务

独立的文件存储模块，支持文件夹层级结构浏览和完整文件操作：

- **存储路径**：`data/user_files/<uid>/`，每个用户独立目录，支持子文件夹层级
- **浏览**：面包屑导航，点击文件夹进入，支持 `..` 返回上级
- **上传限制**：单文件最大 100MB，上传到当前浏览的文件夹（通过 `_path` 字段）
- **文件夹操作**：新建文件夹、删除文件夹（递归）、重命名文件夹
- **文件操作**：上传、下载、预览、删除、重命名
- **在线文本编辑**：支持常见文本/代码文件（.txt/.md/.json/.js/.css/.html/.xml/.yaml/.yml/.py/.sh 等），弹窗内编辑，Ctrl+S 保存
- **权限控制**：
  - 全局开关（`storage_global_enabled`）：管理员在「系统设置」中控制，关闭后全站不可用
  - 用户权限（`storage` 字段）：管理员在用户管理中为每个用户单独授予/撤销
  - **管理员自动拥有权限**，不受 `storage` 字段限制
  - 只有有权限的用户才能创建/删除/重命名/编辑（`requireEditPerm` 中间件），所有用户可浏览/下载/预览
- **文件预览**：图片（jpg/png/gif/svg/webp）、文本（txt/md/json/js/css/html/xml/yaml）、PDF 支持在线预览
- **安全**：`safeUserPath()` 函数双重防护（`path.normalize` + `path.resolve` 范围检查），防止路径穿越攻击
- **防覆盖**：上传同名文件时自动在文件名后追加时间戳

### 新增权限角色

- 修改 `db/init.js` 中 `users.role` 的 `CHECK` 约束
- 在 `layout.hbs` 中添加角色对应的 Navbar 条件分支
- 可参照 `requireAdmin` 在 `middlewares/auth.js` 中新增中间件

### 翻译服务（translate）

百度翻译集成，支持中文、英语、日语三种语言互译：

| 特性 | 说明 |
|---|---|
| 语言选择 | 自定义下拉组件（替代原生 select），支持自动检测源语言 |
| 交换按钮 | 点击交换源语言和目标语言（自动检测时不可交换） |
| 字体 | 输入框和结果框使用衬线字体，17px，阅读舒适 |
| 快捷键 | Ctrl+Enter 快速翻译 |
| API 安全 | AppID 和密钥仅在服务端，前端仅传 {q, from, to} |

### 响应式设计

系统全量适配 PC 和手机端：

| 组件 | PC (>=769px) | 手机 (<=768px) |
|---|---|---|
| 导航栏 | 横向 navbar，含一级链接和下拉菜单 | Hamburger 按钮 → 左侧滑出抽屉侧边栏（260px），带半透明遮罩层 |
| 用户管理 | 标准 HTML 表格（含所有列） | 卡片列表形式，角色/注册时间列隐藏，信息纵向排列 |
| 路由服务 | 完整状态表和操作按钮 | 隐藏配置路径信息行 |
| 翻译服务 | 横向语言选择 + 宽文本区 | 响应式布局，小屏自动缩小间距 |

### 前端注意

- 所有样式在当前内联在 `layout.hbs` 的 `<style>` 中
- 所有视图使用统一的 `.card`、`.btn-*`、`.badge-*`、`.alert-*` 样式
- 新增视图尽量复用这些 CSS 类以保证视觉一致性

### Handlebars 辅助函数

在 `index.js` 中注册了以下自定义 helper：

| 名称 | 用法 | 说明 |
|---|---|---|
| `eq` | `{{#if (eq a b)}}` | 等于比较 |
| `ne` | `{{#if (ne a b)}}` | 不等于比较 |
| `gt` | `{{#if (gt a b)}}` | 大于比较 |
| `and` | `{{#if (and a b)}}` | 逻辑与 |
| `or` | `{{#if (or a b)}}` | 逻辑或 |
| `slice` | `{{slice array start end}}` | 数组切片 |
| `count` | `{{count obj}}` | 获取对象属性数量（用于判断对象是否为空） |
| `keys` | `{{#each (keys obj)}}...{{/each}}` | 遍历对象键 |

## 设计规范 — 手机端名称溢出防护

> 适用范围：所有视图（.hbs）和前端 JavaScript 中涉及用户输入内容（文件名、用户 ID、消息主题、程序名、命令等）的显示区域。

### 核心原则

所有用户输入内容（或可能超长的内容）在手机端（≤768px 宽）显示时，必须采取**溢出截断**措施，防止内容顶出屏幕边界。

### 通用 CSS 类

`layout.hbs` 中已定义全局样式类 `.cell-truncate`：

```css
.cell-truncate {
  display: inline-block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  vertical-align: middle;
}
```

所有需要截断的文本元素应优先使用此样式类，配合 `max-width` 指定宽度。

### 规则列表

| 场景 | 处理方式 | 参考 |
|---|---|---|
| 表格单元格中的文件名/用户名/主题 | 使用 `.cell-truncate` + 适当 `max-width` | `views/message/inbox.hbs`, `views/storage/index.hbs` |
| 弹窗标题中的文件名 | 内联 `max-width: calc(100% - 32px)` 截断 | `views/storage/index.hbs` 预览弹窗 |
| 上传进度文字 | JS 截断文件名至 30 字符 + CSS `text-overflow: ellipsis` | `views/storage/index.hbs` uploadFileDirect() |
| 代码/命令块 | 设 `word-break: break-all` + `word-wrap: break-word` + `overflow-x: auto` | `views/admin/openclaw.hbs` SSH 命令 |
| 消息正文 | 设 `word-break: break-word` + `overflow-wrap: break-word` | `views/message/detail.hbs` |
| 手机端冗余列 | 用 `@media (max-width: 768px)` 隐藏时间列等 | `views/storage/index.hbs`, `views/message/inbox.hbs` |
| 文件名字列 | 始终用 `text-overflow: ellipsis` + 带 `title` 属性显示完整内容 | `views/storage/index.hbs` `.file-name-cell` |

### 新增页面时的检查清单

新增或修改前端模板时，逐项检查：

1. 所有数据绑定（`{{variable}}`、`${variable}`）是否可能产生超长文本？
2. 在手机视口（320px-768px）下，该元素是否会溢出容器？
3. 元素使用 `display: inline` 还是 `display: block/inline-block`？
   - `inline` 元素需要设 `max-width` 才可截断
   - `block/inline-block` 元素设 `max-width` + `overflow: hidden` + `text-overflow: ellipsis`
4. JS 动态拼接的文字（如上传进度、拼接参数）是否也做了截断？
5. 不适合截断的内容（如代码块）是否至少有 `overflow-x: auto` 可滚动查看？
6. 有没有可隐藏的手机端冗余信息（如时间列）？

### 常用 max-width 参考

| 场景 | 建议值 |
|---|---|
| 独立的用户名/文件名（手机端） | 120px |
| 表格文件名列（手机端） | 100px |
| 消息详情主题 | 120px |
| 用户卡片的 UID | 120px |
| 弹窗标题 | `calc(100% - 32px)` |

## 维护

### 重置管理员密码

直接操作 SQLite：
```bash
sqlite3 data/webauth.db
UPDATE users SET password_hash = '$2a$10$...' WHERE uid = 'admin';
```
生成 bcrypt hash：`node -e "console.log(require('bcryptjs').hashSync('新密码', 10))"`

## 通讯录体系

系统包含三种通讯录来源，通过 `/contacts` 页面统一展示：

| 类型 | 维护方式 | 可见范围 | 备注 |
|---|---|---|---|
| 公共通讯录 | 管理员手动添加（`contacts` 表 `type='public'`） | 所有用户 | 可设备注 |
| 全用户通讯录 | 系统自动维护（查询 `users` 表 `is_active=1`） | 拥有 `view_all_contacts` 权限的用户 | 只显示账号、备注为空；新用户自动加入，禁用/删除自动移除 |
| 私有通讯录 | 用户自行添加（`contacts` 表 `type='private'`） | 仅自己可见 | 可设备注，可编辑/删除 |

### 全用户通讯录权限

- **权限字段**：`user_permissions.view_all_contacts`（`1`=可见，`0`=不可见）
- **默认值**：`0`（新用户默认没有该权限）
- **默认配置**：在「系统设置」→「默认权限配置」中修改
- **单个配置**：在「用户管理」→ 权限弹窗中对每个用户单独开关
- **管理员**：默认拥有该权限，且导航栏对管理员隐藏「我的通讯录」入口（管理员通过「账户管理」→「通讯录管理」管理公共通讯录）

### 通讯录管理路由

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/contacts` | 我的通讯录（普通用户可见，管理员不可见） |
| GET | `/contacts/admin` | 通讯录管理（仅管理员，管理公共通讯录） |
| GET | `/contacts/api/list` | 获取当前用户的合并通讯录（JSON） |
| GET | `/contacts/api/public` | 获取公共通讯录（JSON） |
| GET | `/contacts/api/private` | 获取私有通讯录（JSON） |
| GET | `/contacts/api/all-users` | 获取全用户通讯录（JSON，需权限） |
| POST | `/contacts/api/add` | 添加私有联系人 |
| POST | `/contacts/api/remove` | 删除私有联系人 |
| POST | `/contacts/api/note` | 更新私有联系人备注 |
| POST | `/contacts/admin/add` | 管理员添加公共联系人 |
| POST | `/contacts/admin/remove` | 管理员删除公共联系人 |
| POST | `/contacts/admin/note` | 管理员更新公共联系人备注 |

## 删除用户逻辑

管理员删除已申请注销的用户时，按以下顺序手动清理外键关联，避免 `FOREIGN KEY constraint failed`：
1. `contacts`（owner_uid）
2. `messages`（from_uid + to_uid）
3. `user_permissions`（user_id）
4. `project_members`（uid）
5. `project_invitations`（from_uid + to_uid）
6. `projects`（owner_uid）
7. `users`（最终删除）

## 项目命令执行 — 手机端日志

项目命令执行页面（`/projects/:id/commands`）的实时日志中：
- PC 端（>=769px）：显示时间戳、命令名、类型标签、消息正文
- 手机端（<=768px）：时间戳列隐藏（`@media (max-width: 768px) { .log-ts { display: none; } }`），节省窄屏空间

## 扩展指引 — 新增权限

新增权限字段时需同步修改以下位置：

1. **数据库**：`db/init.js` → `user_permissions` 表新增列
2. **Model**：`db/model.js` → `setPermission()`、`setDefaultPermission()`、`setDefaultPermissionsFromMap()`、`ensurePermissionsForUser()` 的 `allowed` 数组中注册字段名
3. **视图 — 权限弹窗**：`views/admin/users.hbs` → JS 中 `fields` 数组添加条目
4. **视图 — 用户个人中心**：`views/user/profile.hbs` → 权限表格中添加对应行
5. **视图 — 默认权限**：`index.js` → `permLabel` helper 中注册中文标签，`views/admin/settings.hbs` 自动渲染
6. **路由**：`routes/projects.js` 或其他需要的地方添加权限检查中间件
