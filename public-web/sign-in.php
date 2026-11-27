<?php
declare(strict_types=1);
require __DIR__ . '/navigation.php';

$options = [
    'register' => ['Create an account', 'Account registration will open when the player account service is available.'],
    'login' => ['Sign in', 'Player sign-in will open when the account service is available.'],
    'activation' => ['Resend activation email', 'Activation emails will be available when account registration opens.'],
    'recover' => ['Forgot password?', 'Password recovery will be available when player accounts open.'],
];
$requested = $_GET['mode'] ?? 'login';
$mode = is_string($requested) && isset($options[$requested]) ? $requested : 'login';
[$title, $message] = $options[$mode];
sitePage($title, 'sign-in.php');
?>
<section class="section account-panel" aria-labelledby="account-heading">
<h1 class="utility-title" id="account-heading"><?= $title ?></h1>
<p class="intro"><?= $message ?></p>
<nav class="account-options" aria-label="Account options">
<?php foreach ($options as $key => [$label]): ?>
<a href="sign-in.php<?= $key === 'login' ? '' : '?mode=' . $key ?>"<?= $key === $mode ? ' aria-current="page"' : '' ?>><?= $label ?></a>
<?php endforeach; ?>
</nav>
</section>
<?php sitePageEnd(); ?>
