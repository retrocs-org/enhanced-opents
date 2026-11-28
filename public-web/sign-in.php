<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';
require __DIR__ . '/accounts.php';

header('Cache-Control: no-store');
header('Referrer-Policy: no-referrer');
session_name('opents_player_session');
session_start([
    'use_strict_mode' => 1,
    'use_only_cookies' => 1,
    'cookie_httponly' => true,
    'cookie_secure' => true,
    'cookie_samesite' => 'Lax',
]);
$_SESSION['account_csrf'] ??= bin2hex(random_bytes(32));
$csrf = $_SESSION['account_csrf'];

$options = [
    'register' => ['Create an account', 'Choose a player name. We’ll email you a link to activate your account and set a password.'],
    'login' => ['Sign in', 'Sign in with your username or email address.'],
    'activation' => ['Resend activation email', 'Enter the email address used to register.'],
    'recover' => ['Forgot password?', 'Enter your email address to request a password reset link.'],
];
$pages = $options + [
    'verify' => ['Activate your account', 'Set a password for your player account.'],
    'reset' => ['Reset your password', 'Set a new password for your player account.'],
];
$requested = $_GET['mode'] ?? 'login';
$mode = is_string($requested) && isset($pages[$requested]) ? $requested : 'login';
[$title, $message] = $pages[$mode];
$token = $_GET['token'] ?? '';
$token = is_string($token) && preg_match('/\A[0-9a-f]{64}\z/', $token) ? $token : '';
$notice = $_SESSION['account_notice'] ?? '';
unset($_SESSION['account_notice']);
$player = null;

function accountField(string $name): string
{
    $value = $_POST[$name] ?? '';
    return is_string($value) ? $value : '';
}

function h(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function accountRedirect(string $notice): never
{
    $_SESSION['account_notice'] = $notice;
    header('Location: sign-in.php', true, 303);
    exit;
}

try {
    $db = null;
    if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
        if (!hash_equals($csrf, accountField('csrf'))) {
            http_response_code(403);
            $notice = 'This form has expired. Reload the page and try again.';
        } elseif (accountField('action') === 'logout') {
            $_SESSION = [];
            session_regenerate_id(true);
            accountRedirect('You’ve signed out.');
        } else {
            $db = database();
            if (!allowAccountAttempt($db, $_SERVER['REMOTE_ADDR'] ?? 'unknown')) {
                http_response_code(429);
                $notice = 'Too many attempts. Try again in a few minutes.';
            } elseif ($mode === 'login') {
                $identity = trim(accountField('identity'));
                $password = accountField('password');
                $account = false;
                if ($identity !== '' && strlen($identity) <= 254 && strlen($password) <= 72) {
                    $account = query($db, 'SELECT id, password_hash, credential_version FROM opents_player
                        WHERE (username = ? OR email = ?) AND password_hash IS NOT NULL', [$identity, $identity])->fetch();
                }
                $dummy = '$2y$12$rjgXcaM6cuYyfR.0j9xyROfBmW.WmiPx41bSEh6RsRGNP0w2a9fFK';
                if (strlen($password) <= 72 && password_verify($password, $account['password_hash'] ?? $dummy) && $account) {
                    session_regenerate_id(true);
                    $_SESSION = ['player_id' => $account['id'], 'player_version' => $account['credential_version']];
                    accountRedirect('You’re signed in.');
                }
                $notice = 'Sign-in failed. Check your credentials and make sure your account is activated.';
            } elseif (in_array($mode, ['verify', 'reset'], true)) {
                $password = accountField('password');
                if (strlen($password) < 12 || strlen($password) > 72 || str_contains($password, "\0")) {
                    $notice = 'Use a password between 12 and 72 bytes.';
                } elseif (acceptAccountPassword($db, $token, $password, $mode === 'reset')) {
                    $_SESSION = [];
                    session_regenerate_id(true);
                    accountRedirect($mode === 'reset' ? 'Password reset. Sign in with your new password.' : 'Account activated. You can now sign in.');
                } else {
                    $notice = 'This link is invalid or expired. Request a new email.';
                }
            } else {
                $email = strtolower(trim(accountField('email')));
                $username = trim(accountField('username'));
                if (strlen($email) > 254 || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
                    $notice = 'Enter a valid email address.';
                } elseif ($mode === 'register' && !preg_match('/\A[A-Za-z0-9_-]{3,24}\z/', $username)) {
                    $notice = 'Use 3–24 letters, numbers, underscores or hyphens for the username.';
                } else {
                    accountMailSettings();
                    if ($mode === 'register') {
                        query($db, 'INSERT INTO opents_player (username, email) VALUES (?, ?)
                            ON DUPLICATE KEY UPDATE id = id', [$username, $email]);
                    }
                    sendAccountLink($db, $email, $mode === 'recover');
                    $notice = 'If this address is eligible, we’ll send an email shortly. Check your spam folder too.';
                }
            }
        }
    }

    if (isset($_SESSION['player_id'], $_SESSION['player_version'])) {
        $db ??= database();
        $player = query($db, 'SELECT username FROM opents_player
            WHERE id = ? AND credential_version = ? AND password_hash IS NOT NULL',
            [$_SESSION['player_id'], $_SESSION['player_version']])->fetchColumn();
        if ($player === false) {
            unset($_SESSION['player_id'], $_SESSION['player_version']);
            $player = null;
        }
    }
} catch (Throwable $error) {
    error_log('Enhanced OpenTS account request failed [' . $error->getCode() . ']');
    http_response_code(503);
    $notice = 'The account service is temporarily unavailable. Try again later.';
}
session_write_close();

$action = 'sign-in.php' . ($mode === 'login' ? '' : '?mode=' . $mode);
if ($token !== '' && in_array($mode, ['verify', 'reset'], true)) {
    $action .= '&token=' . $token;
}
sitePage($title, 'sign-in.php', wide: true);
?>
<section class="hero page-art page-art-account" aria-labelledby="account-heading">
<div class="shell">
<div class="account-panel">
<h1 class="utility-title" id="account-heading"><?= $player !== null && $mode === 'login' ? 'Your account' : h($title) ?></h1>
<?php if ($notice !== ''): ?><p class="empty" role="status"><?= h($notice) ?></p><?php endif; ?>
<?php if ($player !== null && $mode === 'login'): ?>
<p class="intro">Signed in as <strong><?= h($player) ?></strong>.</p>
<form class="account-form" method="post" action="sign-in.php">
<input type="hidden" name="csrf" value="<?= h($csrf) ?>">
<button class="button" name="action" value="logout">Sign out</button>
</form>
<?php elseif (in_array($mode, ['verify', 'reset'], true) && $token === ''): ?>
<p class="intro">This link is invalid. Request a new email below.</p>
<?php else: ?>
<p class="intro"><?= h($message) ?></p>
<form class="account-form" method="post" action="<?= h($action) ?>">
<input type="hidden" name="csrf" value="<?= h($csrf) ?>">
<?php if ($mode === 'register'): ?>
<label>Username <input name="username" required minlength="3" maxlength="24" autocomplete="username"></label>
<?php endif; ?>
<?php if ($mode === 'login'): ?>
<label>Username or email <input name="identity" required maxlength="254" autocomplete="username"></label>
<label>Password <input name="password" type="password" required maxlength="72" autocomplete="current-password"></label>
<?php elseif (in_array($mode, ['verify', 'reset'], true)): ?>
<label>New password <input name="password" type="password" required minlength="12" maxlength="72" autocomplete="new-password"></label>
<?php else: ?>
<label>Email <input name="email" type="email" required maxlength="254" autocomplete="email"></label>
<?php endif; ?>
<button class="button"><?= h($title) ?></button>
</form>
<?php endif; ?>
<nav class="account-options" aria-label="Account options">
<?php foreach ($options as $key => [$label]): ?>
<a href="sign-in.php<?= $key === 'login' ? '' : '?mode=' . $key ?>"<?= $key === $mode ? ' aria-current="page"' : '' ?>><?= h($label) ?></a>
<?php endforeach; ?>
</nav>
</div>
</div>
</section>
<?php sitePageEnd(); ?>
